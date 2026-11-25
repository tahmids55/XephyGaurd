<?php

declare(strict_types=1);

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

    public function consents(): HasMany
    {
        return $this->hasMany(Consent::class);
    }

    public function latestConsent(): HasOne
    {
        return $this->hasOne(Consent::class)->where('purpose', 'research')->latestOfMany('recorded_at');
    }

    public function consentsToResearch(): bool
    {
        return (bool) $this->latestConsent?->granted;
    }

    public static function forLogin(?string $login): ?self
    {
        if (! $login) {
            return null;
        }

        return static::forLogins([$login])[$login] ?? null;
    }

    /**
     * Resolves many logins with one query. Returns a map from each given login to its student
     * (logins with no student are left out). A Windows login DOMAIN\user also matches the short name;
     * when several students match, the lowest id wins, as for forLogin().
     *
     * @param  iterable<string|null>  $logins
     * @return array<string, self>
     */
    public static function forLogins(iterable $logins): array
    {
        $wanted = [];
        foreach ($logins as $login) {
            if (! $login) {
                continue;
            }
            // Windows logins may arrive as DOMAIN\user.
            $wanted[$login] = array_unique([$login, str_contains($login, '\\') ? substr($login, strrpos($login, '\\') + 1) : $login]);
        }
        if ($wanted === []) {
            return [];
        }

        $students = static::query()->whereIn('login', array_unique(array_merge(...array_values($wanted))))->orderBy('id')->get();
        $map = [];
        foreach ($wanted as $login => $candidates) {
            $hit = $students->first(fn (self $s) => in_array($s->login, $candidates, true));
            if ($hit) {
                $map[$login] = $hit;
            }
        }

        return $map;
    }
}
