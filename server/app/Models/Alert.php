<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Database\Eloquent\Relations\HasMany;
use Illuminate\Support\Facades\DB;

class Alert extends Model
{
    protected $fillable = ['pc_id', 'lab_session_id', 'event_id', 'rule_id', 'severity', 'category', 'title', 'details',
        'user', 'occurred_at', 'acknowledged_by', 'acknowledged_at', 'comment'];

    protected $casts = ['details' => 'array', 'occurred_at' => 'datetime', 'acknowledged_at' => 'datetime'];

    public const SEVERITY_POINTS = ['info' => 0, 'low' => 5, 'medium' => 15, 'high' => 30, 'critical' => 50];

    public function pc(): BelongsTo
    {
        return $this->belongsTo(Pc::class);
    }

    public function session(): BelongsTo
    {
        return $this->belongsTo(LabSession::class, 'lab_session_id');
    }

    public function acknowledger(): BelongsTo
    {
        return $this->belongsTo(User::class, 'acknowledged_by');
    }

    public function reviews(): HasMany
    {
        return $this->hasMany(AlertReview::class);
    }

    /**
     * Acknowledge once: appends a review row and stamps the alert. Returns false (and writes nothing)
     * when the alert was already acknowledged, also under a concurrent request.
     */
    public function acknowledgeBy(int $userId, ?string $note): bool
    {
        $now = now();
        $won = DB::transaction(function () use ($userId, $note, $now) {
            $n = static::query()->whereKey($this->id)->whereNull('acknowledged_at')
                ->update(['acknowledged_by' => $userId, 'acknowledged_at' => $now, 'comment' => $note ?? $this->comment]);
            if ($n === 1) {
                AlertReview::create(['alert_id' => $this->id, 'user_id' => $userId, 'note' => $note, 'reviewed_at' => $now]);
            }

            return $n === 1;
        });
        if ($won) {
            $this->refresh();
        }

        return $won;
    }

    public function broadcastPayload(): array
    {
        return [
            'id' => $this->id, 'pc_id' => $this->pc_id, 'pc' => $this->pc?->name, 'rule_id' => $this->rule_id,
            'severity' => $this->severity, 'title' => $this->title, 'user' => $this->user,
            'occurred_at' => $this->occurred_at?->format('H:i:s'), 'session_id' => $this->lab_session_id,
        ];
    }
}
