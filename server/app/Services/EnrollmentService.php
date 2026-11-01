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
            $signed = $this->ca->signClientCsr($e->csr_pem, config('xephyguard.agent_cert_days'));
            $token = Str::random(64);
            // Re-enrolling a known machine (same MAC) keeps its history and seat.
            $pc = Pc::query()->where('lab_id', $e->lab_id)->whereNull('retired_at')->get()
                ->first(fn (Pc $p) => array_intersect($p->macs ?? [], $e->macs ?? []));
            $pc ??= Pc::create([
                'lab_id' => $e->lab_id,
                'name' => $name ?: $e->hostname,
                'hostname' => $e->hostname,
                'status' => 'offline',
            ]);
            $pc->fill(['hostname' => $e->hostname, 'os' => $e->os, 'os_version' => $e->os_version, 'macs' => $e->macs, 'ip' => $e->ip]);
            if ($name) {
                $pc->name = $name;
            }
            $pc->save();
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
