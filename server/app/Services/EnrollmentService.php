<?php

namespace App\Services;

use App\Models\Agent;
use App\Models\Enrollment;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\User;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;

/** "Approve, don't configure": agents register as Pending; staff approve with one click. */
class EnrollmentService
{
    public function __construct(private readonly CertificateAuthority $ca) {}

    /** @return array{enrollment: Enrollment, poll_token: string} */
    public function register(Lab $lab, array $data, string $ip): array
    {
        $pollToken = Str::random(48);
        $macs = array_values(array_unique(array_map('strtolower', $data['macs'] ?? [])));
        $hostname = trim($data['hostname'] ?? '');

        // Cancel/supersede any existing pending enrollments for this machine so teacher sees only the latest
        if ($hostname !== '') {
            Enrollment::query()
                ->where('lab_id', $lab->id)
                ->where('status', 'pending')
                ->whereRaw('LOWER(hostname) = ?', [strtolower($hostname)])
                ->update(['status' => 'superseded', 'decided_at' => now()]);
        }

        $enrollment = Enrollment::create([
            'lab_id' => $lab->id,
            'hostname' => $data['hostname'],
            'os' => $data['os'] ?? null,
            'os_version' => $data['os_version'] ?? null,
            'macs' => $macs,
            'ip' => $ip,
            'agent_version' => $data['agent_version'] ?? null,
            'csr_pem' => $data['csr_pem'],
            'poll_token_hash' => hash('sha256', $pollToken),
        ]);
        if ($lab->auto_approve && array_intersect($macs, array_map('strtolower', $lab->auto_approve_macs ?? []))) {
            $this->approve($enrollment, null);
        }

        return ['enrollment' => $enrollment->fresh(), 'poll_token' => $pollToken];
    }

    public function approve(Enrollment $e, ?User $by, ?string $name = null): Pc
    {
        return DB::transaction(function () use ($e, $by, $name) {
            // If already approved, return associated PC immediately
            if ($e->status === 'approved' && $e->pc_id) {
                return $e->pc ?: Pc::findOrFail($e->pc_id);
            }

            $signed = $this->ca->signClientCsr($e->csr_pem, config('xephyguard.agent_cert_days'));
            $token = Str::random(64);
            $cleanMacs = array_values(array_unique(array_map('strtolower', (array) ($e->macs ?? []))));
            $eHost = strtolower(trim($e->hostname ?? ''));

            // 1. Search active PCs by MAC intersection OR exact hostname match
            $activePcs = Pc::query()->where('lab_id', $e->lab_id)->whereNull('retired_at')->get();
            $pc = $activePcs->first(function (Pc $p) use ($cleanMacs, $eHost) {
                $pMacs = array_map('strtolower', (array) ($p->macs ?? []));
                $macMatch = ! empty($cleanMacs) && ! empty($pMacs) && ! empty(array_intersect($pMacs, $cleanMacs));
                $hostMatch = $eHost !== '' && strtolower(trim($p->hostname ?? '')) === $eHost;
                return $macMatch || $hostMatch;
            });

            // 2. If not found in active PCs, check retired PCs to restore instead of duplicating
            if (! $pc) {
                $retiredPcs = Pc::query()->where('lab_id', $e->lab_id)->whereNotNull('retired_at')->get();
                $pc = $retiredPcs->first(function (Pc $p) use ($cleanMacs, $eHost) {
                    $pMacs = array_map('strtolower', (array) ($p->macs ?? []));
                    $macMatch = ! empty($cleanMacs) && ! empty($pMacs) && ! empty(array_intersect($pMacs, $cleanMacs));
                    $hostMatch = $eHost !== '' && strtolower(trim($p->hostname ?? '')) === $eHost;
                    return $macMatch || $hostMatch;
                });
                if ($pc) {
                    $pc->retired_at = null;
                }
            }

            // 3. If still not found, create a new PC
            $pc ??= Pc::create([
                'lab_id' => $e->lab_id,
                'name' => $name ?: $e->hostname,
                'hostname' => $e->hostname,
                'status' => 'offline',
            ]);

            // Merge MACs and update hardware/network details
            $existingMacs = array_map('strtolower', (array) ($pc->macs ?? []));
            $allMacs = array_values(array_unique(array_merge($existingMacs, $cleanMacs)));

            $pc->fill([
                'hostname' => $e->hostname,
                'os' => $e->os,
                'os_version' => $e->os_version,
                'macs' => $allMacs,
                'ip' => $e->ip,
                'retired_at' => null,
            ]);
            if ($name) {
                $pc->name = $name;
            }
            $pc->save();

            // 4. Retire ANY other active PC records in this lab with the same hostname or MACs
            if ($eHost !== '') {
                Pc::query()
                    ->where('lab_id', $e->lab_id)
                    ->where('id', '!=', $pc->id)
                    ->whereNull('retired_at')
                    ->where(function ($q) use ($eHost) {
                        $q->whereRaw('LOWER(hostname) = ?', [$eHost]);
                    })
                    ->update(['retired_at' => now()]);
            }

            // 5. Invalidate any other pending enrollments for this hostname
            if ($eHost !== '') {
                Enrollment::query()
                    ->where('lab_id', $e->lab_id)
                    ->where('id', '!=', $e->id)
                    ->where('status', 'pending')
                    ->whereRaw('LOWER(hostname) = ?', [$eHost])
                    ->update(['status' => 'superseded', 'decided_at' => now()]);
            }

            // 6. Revoke existing agent tokens for this PC and issue the new one
            Agent::query()->where('pc_id', $pc->id)->whereNull('revoked_at')->update(['revoked_at' => now()]);
            Agent::create([
                'pc_id' => $pc->id,
                'token_hash' => hash('sha256', $token),
                'cert_serial' => $signed['serial'],
                'cert_fingerprint' => $signed['fingerprint'],
                'cert_expires_at' => $signed['expires_at'],
                'version' => $e->agent_version,
            ]);
            $e->forceFill([
                'status' => 'approved', 'pc_id' => $pc->id, 'cert_pem' => $signed['pem'], 'issued_token' => $token,
                'decided_by' => $by?->id, 'decided_at' => now(),
            ])->save();
            Audit::log('enrollment.approve', $e, ['pc_id' => $pc->id, 'hostname' => $e->hostname, 'auto' => $by === null]);

            return $pc;
        });
    }

    public function reject(Enrollment $e, User $by): void
    {
        $e->forceFill(['status' => 'rejected', 'decided_by' => $by->id, 'decided_at' => now()])->save();
        Audit::log('enrollment.reject', $e, ['hostname' => $e->hostname]);
    }

    /** What the polling agent sees. The token is handed out exactly once. */
    public function poll(Enrollment $e, string $pollToken): ?array
    {
        if (! hash_equals($e->poll_token_hash, hash('sha256', $pollToken))) {
            return null;
        }
        if ($e->status !== 'approved') {
            return ['status' => $e->status];
        }
        if ($e->issued_token === null) {
            return ['status' => 'collected'];   // already delivered; re-enroll to get new credentials
        }
        $out = [
            'status' => 'approved',
            'pc_id' => $e->pc_id,
            'name' => $e->pc?->name,
            'token' => $e->issued_token,
            'cert_pem' => $e->cert_pem,
            'ca_pem' => $this->ca->certPem(),
        ];
        $e->forceFill(['issued_token' => null])->save();

        return $out;
    }
}
