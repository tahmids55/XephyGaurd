<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class EnrollmentKey extends Model
{
    protected $fillable = [
        'lab_id',
        'label',
        'key_prefix',
        'key_hash',
        'expires_at',
        'max_uses',
        'uses_count',
        'allowed_macs',
        'revoked_at',
        'created_by',
    ];

    protected $hidden = ['key_hash'];

    protected $casts = [
        'expires_at' => 'datetime',
        'revoked_at' => 'datetime',
        'allowed_macs' => 'array',
        'max_uses' => 'integer',
        'uses_count' => 'integer',
    ];

    public function lab(): BelongsTo
    {
        return $this->belongsTo(Lab::class);
    }

    public function creator(): BelongsTo
    {
        return $this->belongsTo(User::class, 'created_by');
    }

    /**
     * Issues a new short-lived, revocable per-lab enrollment key.
     *
     * @param  ?list<string>  $allowedMacs
     * @return array{model: self, plain: string}
     */
    public static function issue(
        Lab $lab,
        string $label,
        ?int $ttlHours = 24,
        ?int $maxUses = null,
        ?array $allowedMacs = null,
        ?User $by = null
    ): array {
        $plain = 'XG-'.strtoupper(implode('-', str_split(bin2hex(random_bytes(9)), 6)));
        $prefix = substr($plain, 0, 9);
        $normalizedMacs = null;
        if (! empty($allowedMacs)) {
            $normalizedMacs = collect($allowedMacs)
                ->map(fn ($m) => strtolower(trim((string) $m)))
                ->filter(fn ($m) => (bool) preg_match('/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/', $m))
                ->unique()
                ->values()
                ->all();
            if ($normalizedMacs === []) {
                $normalizedMacs = null;
            }
        }

        $model = static::query()->create([
            'lab_id' => $lab->id,
            'label' => $label,
            'key_prefix' => $prefix,
            'key_hash' => hash('sha256', $plain),
            'expires_at' => ($ttlHours !== null && $ttlHours > 0) ? now()->addHours($ttlHours) : null,
            'max_uses' => ($maxUses !== null && $maxUses > 0) ? $maxUses : null,
            'uses_count' => 0,
            'allowed_macs' => $normalizedMacs,
            'created_by' => $by?->id,
        ]);

        return ['model' => $model, 'plain' => $plain];
    }

    /**
     * Checks whether this key is currently active and eligible for the given MAC addresses.
     *
     * @param  list<string>  $macs
     */
    public function isValidFor(array $macs = []): bool
    {
        if ($this->revoked_at !== null) {
            return false;
        }
        if ($this->expires_at !== null && $this->expires_at->lte(now())) {
            return false;
        }
        if ($this->max_uses !== null && $this->uses_count >= $this->max_uses) {
            return false;
        }
        if (! empty($this->allowed_macs)) {
            $allowed = array_map(fn ($m) => strtolower(trim((string) $m)), $this->allowed_macs);
            $provided = array_map(fn ($m) => strtolower(trim((string) $m)), $macs);
            if (array_intersect($allowed, $provided) === []) {
                return false;
            }
        }

        return true;
    }

    public function revoke(): void
    {
        if ($this->revoked_at === null) {
            $this->update(['revoked_at' => now()]);
        }
    }
}
