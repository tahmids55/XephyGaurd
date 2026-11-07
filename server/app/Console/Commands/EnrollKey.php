<?php

namespace App\Console\Commands;

use App\Models\EnrollmentKey;
use App\Models\Lab;
use Illuminate\Console\Command;

class EnrollKey extends Command
{
    protected $signature = 'xephyguard:enroll-key
        {--rotate : Rotate the legacy lab enrollment key}
        {--create= : Create a short-lived revocable enrollment key with the given label}
        {--ttl=24 : Time-to-live in hours for --create}
        {--max-uses= : Maximum allowed enrollments for --create}
        {--macs= : Comma-separated allowed MAC addresses for --create}
        {--revoke= : Revoke an enrollment key by ID or prefix}
        {--list : List all short-lived enrollment keys}';

    protected $description = 'Manage short-lived revocable enrollment keys (or show/rotate the lab enrollment key)';

    public function handle(): int
    {
        $lab = Lab::current();

        if ($revokeTarget = $this->option('revoke')) {
            $key = EnrollmentKey::query()
                ->where('lab_id', $lab->id)
                ->where(fn ($q) => $q->where('id', $revokeTarget)->orWhere('key_prefix', $revokeTarget))
                ->first();
            if (! $key) {
                $this->error("Enrollment key '{$revokeTarget}' not found.");

                return self::FAILURE;
            }
            $key->revoke();
            $this->info("Revoked enrollment key #{$key->id} ({$key->key_prefix} - {$key->label}).");

            return self::SUCCESS;
        }

        if ($label = $this->option('create')) {
            $ttl = $this->option('ttl') !== null ? (int) $this->option('ttl') : 24;
            $maxUses = $this->option('max-uses') !== null && $this->option('max-uses') !== '' ? (int) $this->option('max-uses') : null;
            $macs = $this->option('macs') ? preg_split('/[\s,;]+/', (string) $this->option('macs')) : null;
            $issued = EnrollmentKey::issue($lab, (string) $label, $ttl, $maxUses, $macs);
            $this->line($issued['plain']);

            return self::SUCCESS;
        }

        if ($this->option('list')) {
            $keys = EnrollmentKey::query()->where('lab_id', $lab->id)->orderByDesc('id')->get();
            $this->table(
                ['ID', 'Prefix', 'Label', 'Uses', 'Expires', 'Status'],
                $keys->map(fn (EnrollmentKey $k) => [
                    $k->id,
                    $k->key_prefix,
                    $k->label,
                    $k->uses_count.($k->max_uses ? '/'.$k->max_uses : ''),
                    $k->expires_at?->toDateTimeString() ?? 'never',
                    $k->revoked_at ? 'revoked' : ($k->isValidFor($k->allowed_macs ?? []) ? 'active' : 'expired/exhausted'),
                ])->all()
            );

            return self::SUCCESS;
        }

        $this->line($this->option('rotate') ? $lab->rotateEnrollKey() : ($lab->enrollKey() ?? $lab->rotateEnrollKey()));

        return self::SUCCESS;
    }
}
