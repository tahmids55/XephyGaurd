<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Builder;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Support\Str;

/** One-time per-PC enrollment token (DECISIONS #28). Only its sha256 is stored. */
class EnrollmentToken extends Model
{
    public const PREFIX = 'xgen_';

    protected $fillable = ['lab_id', 'token_hash', 'pc_name', 'created_by', 'expires_at', 'used_at', 'enrollment_id', 'revoked_at'];

    protected $hidden = ['token_hash'];

    protected $casts = ['expires_at' => 'datetime', 'used_at' => 'datetime', 'revoked_at' => 'datetime'];

    /** @return array{0: EnrollmentToken, 1: string} the row and the plain token (shown once) */
    public static function issue(Lab $lab, ?User $by, ?string $pcName, int $hours): array
    {
        $plain = self::PREFIX.Str::random(40);
        $row = self::create([
            'lab_id' => $lab->id,
            'token_hash' => hash('sha256', $plain),
            'pc_name' => $pcName !== null && trim($pcName) !== '' ? trim($pcName) : null,
            'created_by' => $by?->id,
            'expires_at' => now()->addHours(max(1, min($hours, 24 * 30))),
        ]);

        return [$row, $plain];
    }

    public function scopeUsable(Builder $q): Builder
    {
        return $q->whereNull('used_at')->whereNull('revoked_at')->where('expires_at', '>', now());
    }

    /** Finds an unused, unexpired token of the lab by its plain value. */
    public static function findUsable(Lab $lab, string $plain): ?self
    {
        if (! str_starts_with($plain, self::PREFIX) || strlen($plain) > 128) {
            return null;
        }

        return self::query()->usable()->where('lab_id', $lab->id)->where('token_hash', hash('sha256', $plain))->first();
    }

    /** Marks the token used by this enrollment. False if another request used it first (race-safe). */
    public function consume(Enrollment $e): bool
    {
        return self::query()->whereKey($this->id)->usable()
            ->update(['used_at' => now(), 'enrollment_id' => $e->id, 'updated_at' => now()]) === 1;
    }

    public function enrollment(): BelongsTo
    {
        return $this->belongsTo(Enrollment::class);
    }

    public function creator(): BelongsTo
    {
        return $this->belongsTo(User::class, 'created_by');
    }
}
