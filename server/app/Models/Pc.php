<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Builder;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Database\Eloquent\Relations\HasMany;
use Illuminate\Database\Eloquent\Relations\HasOne;

class Pc extends Model
{
    protected $fillable = ['lab_id', 'seat_id', 'name', 'hostname', 'machine_uid', 'os', 'os_version', 'macs', 'ip', 'status', 'mode',
        'current_user', 'active_app', 'last_event', 'live_score', 'isolated', 'last_seen_at', 'clean_shutdown',
        'capabilities', 'usage', 'retired_at', 'silent_alerted_at'];

    protected $casts = [
        'macs' => 'array', 'capabilities' => 'array', 'usage' => 'array', 'isolated' => 'boolean',
        'clean_shutdown' => 'boolean', 'last_seen_at' => 'datetime', 'retired_at' => 'datetime',
        'silent_alerted_at' => 'datetime',
    ];

    public function lab(): BelongsTo
    {
        return $this->belongsTo(Lab::class);
    }

    public function seat(): BelongsTo
    {
        return $this->belongsTo(Seat::class);
    }

    public function agent(): HasOne
    {
        return $this->hasOne(Agent::class)->whereNull('revoked_at')->latestOfMany();
    }

    public function agents(): HasMany
    {
        return $this->hasMany(Agent::class);
    }

    public function alerts(): HasMany
    {
        return $this->hasMany(Alert::class);
    }

    public function events(): HasMany
    {
        return $this->hasMany(Event::class);
    }

    public function evidenceCaptures(): HasMany
    {
        return $this->hasMany(EvidenceCapture::class);
    }

    public function scopeActive(Builder $q): Builder
    {
        return $q->whereNull('retired_at');
    }

    /** Maps DB tile status, risk score, and active user into Live lab seat state. */
    public static function resolveUiStatus(?string $status, int $score, ?string $user): string
    {
        $status = $status ?: 'normal';
        if ($status === 'offline') {
            return 'offline';
        }
        if ($user === '' || $user === null) {
            return 'idle';
        }
        if ($score >= 50 || $status === 'alert') {
            return 'alert';
        }
        if ($score >= 25 || in_array($status, ['watch', 'warning'], true)) {
            return 'watch';
        }

        return 'normal';
    }

    public function online(): bool
    {
        return $this->last_seen_at !== null &&
            $this->last_seen_at->gt(now()->subSeconds(config('xephyguard.heartbeat_timeout_s')));
    }

    /** Data shown on a live-view tile (also the payload of the PcUpdated broadcast). */
    /** "6.5 fps · 820 kbit/s · 5.9 % CPU · throttle 3" from the last heartbeat's usage.screen, null while not streaming. */
    public function screenCostLabel(): ?string
    {
        $s = is_array($this->usage['screen'] ?? null) ? $this->usage['screen'] : null;
        if (! $s) {
            return null;
        }
        $parts = [];
        if (isset($s['fps'])) {
            $parts[] = round((float) $s['fps'], 1).' fps';
        }
        if (isset($s['kbps'])) {
            $parts[] = (int) $s['kbps'].' kbit/s';
        }
        if (isset($s['cpu_pct'])) {
            $parts[] = round((float) $s['cpu_pct'], 1).' % CPU';
        }
        if (isset($s['level'])) {
            $parts[] = 'throttle '.(int) $s['level'];
        }

        return $parts ? implode(' · ', $parts) : null;
    }

    public function tile(): array
    {
        return [
            'id' => $this->id,
            'name' => $this->name,
            'seat_id' => $this->seat_id,
            'status' => $this->online() ? $this->status : 'offline',
            'mode' => $this->mode,
            'user' => $this->current_user,
            'app' => $this->active_app,
            'last_event' => $this->last_event,
            'score' => $this->live_score,
            'isolated' => $this->isolated,
            'os' => $this->os,
            'last_seen' => $this->last_seen_at?->toIso8601String(),
        ];
    }
}
