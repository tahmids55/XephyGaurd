<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\HasMany;

class Lab extends Model
{
    protected $fillable = ['name', 'subnet', 'ccc_ip', 'auto_approve', 'auto_approve_macs', 'grid_cols', 'grid_rows'];

    protected $hidden = ['enroll_key_hash'];

    protected $casts = ['auto_approve' => 'boolean', 'auto_approve_macs' => 'array', 'enroll_key_rotated_at' => 'datetime'];

    public function pcs(): HasMany
    {
        return $this->hasMany(Pc::class);
    }

    public function seats(): HasMany
    {
        return $this->hasMany(Seat::class);
    }

    public function sessions(): HasMany
    {
        return $this->hasMany(LabSession::class);
    }

    public static function current(): self
    {
        return static::query()->orderBy('id')->firstOrFail();
    }

    /** Generates a new enrollment key, stores only its hash, returns the plaintext once. */
    public function rotateEnrollKey(): string
    {
        $key = 'XG-'.strtoupper(implode('-', str_split(bin2hex(random_bytes(9)), 6)));
        $this->forceFill([
            'enroll_key_hash' => hash('sha256', $key),
            'enroll_key_hint' => substr($key, -4),
            'enroll_key_rotated_at' => now(),
        ])->save();
        Setting::put('enroll_key_plain', encrypt($key));   // shown on the Add-a-PC page to staff only
        // A rotation is the "start over" switch: unused one-time tokens die with the old key (S-18, DECISIONS #28).
        EnrollmentToken::query()->usable()->where('lab_id', $this->id)->update(['revoked_at' => now(), 'updated_at' => now()]);

        return $key;
    }

    public function enrollKey(): ?string
    {
        $enc = Setting::get('enroll_key_plain');
        if (! $enc) {
            return null;
        }
        try {
            return decrypt($enc);
        } catch (\Throwable) {
            return null;
        }
    }

    public function checkEnrollKey(string $key): bool
    {
        return $this->enroll_key_hash !== null && hash_equals($this->enroll_key_hash, hash('sha256', trim($key)));
    }
}
