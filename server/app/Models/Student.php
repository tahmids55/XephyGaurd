<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\HasMany;
use Illuminate\Database\Eloquent\Relations\HasOne;

class Student extends Model
{
    protected $fillable = ['student_number', 'name', 'login', 'pseudonym', 'exempt', 'exempt_reason'];
    protected $casts = ['exempt' => 'boolean'];

    protected static function booted(): void
    {
        static::creating(function (self $s) {
            $s->pseudonym ??= 'S-'.strtoupper(bin2hex(random_bytes(4)));
        });
    }

    public function consents(): HasMany { return $this->hasMany(Consent::class); }
    public function latestConsent(): HasOne { return $this->hasOne(Consent::class)->where('purpose', 'research')->latestOfMany('recorded_at'); }

    public function consentsToResearch(): bool
    {
        return (bool) $this->latestConsent?->granted;
    }

    public static function forLogin(?string $login): ?self
    {
        if (! $login) {
            return null;
        }
        // Windows logins may arrive as DOMAIN\user.
        $short = str_contains($login, '\\') ? substr($login, strrpos($login, '\\') + 1) : $login;

        return static::query()->whereIn('login', array_unique([$login, $short]))->first();
    }
}
