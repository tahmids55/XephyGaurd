<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\HasMany;
use Illuminate\Database\Eloquent\Relations\HasOne;
use Illuminate\Support\Str;

class Student extends Model
{
    protected $fillable = ['student_number', 'name', 'login', 'pseudonym', 'portal_token', 'exempt', 'exempt_reason', 'legal_hold'];
    protected $casts = ['exempt' => 'boolean', 'legal_hold' => 'boolean'];

    protected static function booted(): void
    {
        static::creating(function (self $s) {
            $s->pseudonym ??= 'S-'.strtoupper(bin2hex(random_bytes(4)));
            $s->portal_token ??= Str::random(48);
        });
    }

    public function ensurePortalToken(): string
    {
        if (! $this->portal_token) {
            $this->portal_token = Str::random(48);
            $this->save();
        }

        return $this->portal_token;
    }

    public function appeals(): HasMany { return $this->hasMany(Appeal::class); }
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
