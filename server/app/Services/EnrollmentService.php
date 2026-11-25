<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Agent;
use App\Models\Enrollment;
use App\Models\EnrollmentToken;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\User;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Str;
use InvalidArgumentException;

/** "Approve, don't configure": agents register as Pending; staff approve with one click, or a one-time token approves. */
class EnrollmentService
{
    public function __construct(private readonly CertificateAuthority $ca) {}

    /** @return array{enrollment: Enrollment, poll_token: string} */
    public function register(Lab $lab, array $data, string $ip, ?EnrollmentToken $enrollToken = null): array
    {
        $pollToken = Str::random(48);
        $macs = array_values(array_unique(array_map('strtolower', $data['macs'] ?? [])));
        $hostname = trim($data['hostname'] ?? '');
        $uid = self::normalizeUid($data['machine_uid'] ?? null);

        // Supersede older pending requests of the same machine (exact machine_uid only) so staff see the latest.
        if ($uid !== null) {
            Enrollment::query()
                ->where('lab_id', $lab->id)
                ->where('status', 'pending')
                ->where('machine_uid', $uid)
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
        // Flag (never merge) a PC that only looks like this machine (same hostname or MAC, other or no uid).
        $conflict = $this->conflictFor($lab->id, $uid, $hostname, $macs);
        if ($conflict !== null) {
            $enrollment->forceFill(['conflict_pc_id' => $conflict->id])->save();
        }
        if ($enrollToken !== null) {
            // consume() is atomic: if another request used the token first, this one stays pending.
            if ($enrollToken->consume($enrollment)) {
                $this->approve($enrollment, null, $enrollToken->pc_name, 'token');
            }
        }
        // MAC addresses never approve anything (spoofable; S-16, DECISIONS #28). Staff click or token only.

        return ['enrollment' => $enrollment->fresh(), 'poll_token' => $pollToken];
    }

    /**
     * Approves an enrollment. An existing PC is reused only when its machine_uid equals the enrollment's,
     * or when staff explicitly pick it ($adopt). Look-alikes (hostname/MAC) are flagged, never merged (S-17).
     */
    public function approve(Enrollment $e, ?User $by, ?string $name = null, string $via = 'staff', ?Pc $adopt = null): Pc
    {
        return DB::transaction(function () use ($e, $by, $name, $via, $adopt) {
            // If already approved, return associated PC immediately
            if ($e->status === 'approved' && $e->pc_id) {
                return $e->pc ?: Pc::findOrFail($e->pc_id);
            }

            $signed = $this->ca->signClientCsr($e->csr_pem, config('xephyguard.agent_cert_days'));
            $token = Str::random(64);
            $cleanMacs = array_values(array_unique(array_map('strtolower', (array) ($e->macs ?? []))));

            $uid = self::normalizeUid($e->machine_uid);
            if ($adopt !== null) {
                if ((int) $adopt->lab_id !== (int) $e->lab_id || ($adopt->machine_uid !== null && $adopt->machine_uid !== $uid)) {
                    throw new InvalidArgumentException('That PC belongs to another machine; it cannot be adopted.');
                }
                if ($uid !== null && Pc::query()->where('machine_uid', $uid)->whereKeyNot($adopt->id)->exists()) {
                    throw new InvalidArgumentException('This machine is already enrolled as another PC.');
                }
                $pc = $adopt;
            } else {
                $pc = $uid === null ? null : Pc::query()->where('lab_id', $e->lab_id)->where('machine_uid', $uid)
                    ->orderByRaw('retired_at IS NOT NULL')->orderByDesc('id')->first();
            }
            $conflict = $pc === null ? $this->conflictFor((int) $e->lab_id, $uid, (string) $e->hostname, $cleanMacs) : null;

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

            // Invalidate other pending enrollments of the same machine.
            $pending = Enrollment::query()
                ->where('lab_id', $e->lab_id)
                ->where('id', '!=', $e->id)
                ->where('status', 'pending');
            if ($uid !== null) {
                $pending->where('machine_uid', $uid)->update(['status' => 'superseded', 'decided_at' => now()]);
            }

            // 6. Revoke existing agent tokens for this PC and issue the new one
            Agent::query()->where('pc_id', $pc->id)->whereNull('revoked_at')->update(['revoked_at' => now()]);
            Agent::create([
                'pc_id' => $pc->id,
                'token_hash' => Agent::tokenHash($token, $signed['fingerprint']),
                'cert_serial' => $signed['serial'],
                'cert_fingerprint' => $signed['fingerprint'],
                'cert_expires_at' => $signed['expires_at'],
                'version' => $e->agent_version,
            ]);
            $e->forceFill([
                'status' => 'approved', 'pc_id' => $pc->id, 'cert_pem' => $signed['pem'], 'issued_token' => $token,
                'decided_by' => $by?->id, 'decided_at' => now(),
            ])->save();
            Audit::log('enrollment.approve', $e, ['pc_id' => $pc->id, 'hostname' => $e->hostname, 'auto' => $by === null, 'via' => $via, 'new_pc' => $isNew, 'machine_uid' => $uid,
                'adopted' => $adopt !== null, 'conflict_pc_id' => $conflict?->id]);

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

    /** An active PC that looks like this machine (hostname or MAC) but is not identified by its uid. */
    private function conflictFor(int $labId, ?string $uid, string $hostname, array $macs): ?Pc
    {
        $host = strtolower(trim($hostname));
        $macs = array_map('strtolower', $macs);

        return Pc::query()->where('lab_id', $labId)->whereNull('retired_at')
            ->where(fn ($q) => $uid === null ? $q : $q->whereNull('machine_uid')->orWhere('machine_uid', '!=', $uid))
            ->get()->first(function (Pc $p) use ($host, $macs) {
                $pMacs = array_map('strtolower', (array) ($p->macs ?? []));

                return ($host !== '' && strtolower(trim((string) $p->hostname)) === $host)
                    || ($macs !== [] && array_intersect($pMacs, $macs) !== []);
            });
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
