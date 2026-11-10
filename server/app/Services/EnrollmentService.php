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
        $uid = self::normalizeUid($data['machine_uid'] ?? null);

        // Cancel/supersede any existing pending enrollments for this machine so teacher sees only the latest.
        // With a machine_uid the machine is identified exactly; hostname is only a legacy fallback because
        // cloned lab images share hostnames.
        if ($uid !== null) {
            Enrollment::query()
                ->where('lab_id', $lab->id)
                ->where('status', 'pending')
                ->where('machine_uid', $uid)
                ->update(['status' => 'superseded', 'decided_at' => now()]);
        } elseif ($hostname !== '') {
            Enrollment::query()
                ->where('lab_id', $lab->id)
                ->where('status', 'pending')
                ->whereNull('machine_uid')
                ->whereRaw('LOWER(hostname) = ?', [strtolower($hostname)])
                ->update(['status' => 'superseded', 'decided_at' => now()]);
        }

        $enrollment = Enrollment::create([
            'lab_id' => $lab->id,
            'hostname' => $data['hostname'],
            'machine_uid' => $uid,
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

            $uid = self::normalizeUid($e->machine_uid);
            $matches = function (Pc $p) use ($cleanMacs, $eHost) {
                $pMacs = array_map('strtolower', (array) ($p->macs ?? []));
                $macMatch = ! empty($cleanMacs) && ! empty($pMacs) && ! empty(array_intersect($pMacs, $cleanMacs));
                $hostMatch = $eHost !== '' && strtolower(trim($p->hostname ?? '')) === $eHost;

                return [$macMatch, $hostMatch];
            };

            $pc = null;
            if ($uid !== null) {
                // Exact identity: the same installation re-enrolling (active first, then retired).
                $pc = Pc::query()->where('lab_id', $e->lab_id)->where('machine_uid', $uid)
                    ->orderByRaw('retired_at IS NOT NULL')->orderByDesc('id')->first();

                // One-time adoption of a pre-uid PC row: only when hostname AND a MAC match and exactly one
                // such row exists, so identical clones are never merged.
                if (! $pc) {
                    $legacy = Pc::query()->where('lab_id', $e->lab_id)->whereNull('machine_uid')->whereNull('retired_at')
                        ->get()->filter(function (Pc $p) use ($matches) {
                            [$m, $h] = $matches($p);

                            return $m && $h;
                        });
                    if ($legacy->count() === 1) {
                        $pc = $legacy->first();
                    }
                }
            } else {
                // Legacy agent without machine_uid: previous behaviour (MAC intersection OR hostname).
                $pc = Pc::query()->where('lab_id', $e->lab_id)->whereNull('retired_at')->get()
                    ->first(function (Pc $p) use ($matches) {
                        return in_array(true, $matches($p), true);
                    });
                if (! $pc) {
                    $pc = Pc::query()->where('lab_id', $e->lab_id)->whereNotNull('retired_at')->get()
                        ->first(function (Pc $p) use ($matches) {
                            return in_array(true, $matches($p), true);
                        });
                }
            }

            // Create a new PC when nothing matched. Duplicate display names get a numeric suffix.
            $isNew = ! $pc;
            $pc ??= Pc::create([
                'lab_id' => $e->lab_id,
                'name' => $name ?: $this->uniqueName($e->lab_id, (string) $e->hostname),
                'hostname' => $e->hostname,
                'machine_uid' => $uid,
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
            if ($uid !== null) {
                $pc->machine_uid = $uid;
            }
            if ($name) {
                $pc->name = $name;
            }
            $pc->save();

            // Legacy agents only: retire other rows of the same hostname (a uid-identified PC never does this,
            // because identical hostnames are different machines).
            if ($uid === null && $eHost !== '') {
                Pc::query()
                    ->where('lab_id', $e->lab_id)
                    ->where('id', '!=', $pc->id)
                    ->whereNull('retired_at')
                    ->whereNull('machine_uid')
                    ->whereRaw('LOWER(hostname) = ?', [$eHost])
                    ->update(['retired_at' => now()]);
            }

            // Invalidate other pending enrollments of the same machine.
            $pending = Enrollment::query()
                ->where('lab_id', $e->lab_id)
                ->where('id', '!=', $e->id)
                ->where('status', 'pending');
            if ($uid !== null) {
                $pending->where('machine_uid', $uid)->update(['status' => 'superseded', 'decided_at' => now()]);
            } elseif ($eHost !== '') {
                $pending->whereNull('machine_uid')->whereRaw('LOWER(hostname) = ?', [$eHost])
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
            Audit::log('enrollment.approve', $e, ['pc_id' => $pc->id, 'hostname' => $e->hostname, 'auto' => $by === null, 'new_pc' => $isNew, 'machine_uid' => $uid]);

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

    private static function normalizeUid(?string $uid): ?string
    {
        $uid = strtolower(trim((string) $uid));

        return preg_match('/^[0-9a-f]{32}$/', $uid) ? $uid : null;
    }

    private function uniqueName(int $labId, string $base): string
    {
        $base = mb_substr(trim($base) !== '' ? trim($base) : 'PC', 0, 56);
        $taken = Pc::query()->where('lab_id', $labId)->whereNull('retired_at')->pluck('name')
            ->map(fn ($n) => strtolower($n))->all();
        $name = $base;
        for ($i = 2; in_array(strtolower($name), $taken, true); $i++) {
            $name = $base.' ('.$i.')';
        }

        return $name;
    }
}
