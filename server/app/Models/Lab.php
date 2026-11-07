<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\HasMany;

class Lab extends Model
{
    protected $fillable = ['name', 'subnet', 'ccc_ip', 'auto_approve', 'auto_approve_macs', 'grid_cols', 'grid_rows'];
    protected $hidden = ['enroll_key_hash'];
    protected $casts = ['auto_approve' => 'boolean', 'auto_approve_macs' => 'array', 'enroll_key_rotated_at' => 'datetime'];

    public function pcs(): HasMany { return $this->hasMany(Pc::class); }
    public function seats(): HasMany { return $this->hasMany(Seat::class); }
    public function sessions(): HasMany { return $this->hasMany(LabSession::class); }
    public function enrollmentKeys(): HasMany { return $this->hasMany(EnrollmentKey::class); }

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

        return $key;
    }

    public function enrollKey(): ?string
    {
        $enc = Setting::get('enroll_key_plain');

        return $enc ? decrypt($enc) : null;
    }

    public function checkEnrollKey(string $key): bool
    {
        return $this->enroll_key_hash !== null && hash_equals($this->enroll_key_hash, hash('sha256', trim($key)));
    }

    /**
     * Verifies and consumes an enrollment key against short-lived EnrollmentKey records first,
     * falling back to the legacy lab enrollment key hash if not matched in enrollment_keys.
     *
     * @param  list<string>  $macs
     */
    public function verifyAndConsumeEnrollKey(string $plainKey, array $macs = []): bool
    {
        $hash = hash('sha256', trim($plainKey));
        $record = EnrollmentKey::query()
            ->where('lab_id', $this->id)
            ->where('key_hash', $hash)
            ->first();

        if ($record !== null) {
            if (! $record->isValidFor($macs)) {
                return false;
            }
            $record->increment('uses_count');

            return true;
        }

        return $this->checkEnrollKey($plainKey);
    }
}
