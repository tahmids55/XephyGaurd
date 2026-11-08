<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Alert extends Model
{
    protected $fillable = ['pc_id', 'lab_session_id', 'event_id', 'rule_id', 'severity', 'category', 'title', 'details',
        'user', 'occurred_at', 'acknowledged_by', 'acknowledged_at', 'comment'];
    protected $casts = ['details' => 'array', 'occurred_at' => 'datetime', 'acknowledged_at' => 'datetime'];

    public const SEVERITY_POINTS = ['info' => 0, 'low' => 5, 'medium' => 15, 'high' => 30, 'critical' => 50];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
    public function session(): BelongsTo { return $this->belongsTo(LabSession::class, 'lab_session_id'); }
    public function acknowledger(): BelongsTo { return $this->belongsTo(User::class, 'acknowledged_by'); }

    public function broadcastPayload(): array
    {
        return [
            'id' => $this->id, 'pc_id' => $this->pc_id, 'pc' => $this->pc?->name, 'rule_id' => $this->rule_id,
            'severity' => $this->severity, 'title' => $this->title, 'user' => $this->user,
            'occurred_at' => $this->occurred_at?->format('H:i:s'), 'session_id' => $this->lab_session_id,
        ];
    }
}
