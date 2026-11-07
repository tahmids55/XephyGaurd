<?php

namespace App\Models;

use App\Services\EventIngestor;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Alert extends Model
{
    protected $fillable = ['pc_id', 'lab_session_id', 'event_id', 'rule_id', 'severity', 'category', 'title', 'details',
        'user', 'occurred_at', 'acknowledged_by', 'acknowledged_at', 'comment'];
    protected $casts = ['details' => 'array', 'occurred_at' => 'datetime', 'acknowledged_at' => 'datetime'];
    protected $appends = ['evidence_meta', 'event_class', 'confidence'];

    public const SEVERITY_POINTS = ['info' => 0, 'low' => 5, 'medium' => 15, 'high' => 30, 'critical' => 50];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
    public function session(): BelongsTo { return $this->belongsTo(LabSession::class, 'lab_session_id'); }
    public function acknowledger(): BelongsTo { return $this->belongsTo(User::class, 'acknowledged_by'); }

    public function getEvidenceMetaAttribute(): array
    {
        $d = is_array($this->details) ? $this->details : [];
        if (isset($d['evidence_meta']) && is_array($d['evidence_meta'])) {
            return $d['evidence_meta'];
        }

        return EventIngestor::classifyEvidence('rule_hit', $d + [
            'rule_id' => $this->rule_id,
            'category' => $this->category,
        ]);
    }

    public function getEventClassAttribute(): string
    {
        return $this->evidence_meta['event_class'];
    }

    public function getConfidenceAttribute(): string
    {
        return $this->evidence_meta['confidence'];
    }

    public function broadcastPayload(): array
    {
        $meta = $this->evidence_meta;

        return [
            'id' => $this->id, 'pc_id' => $this->pc_id, 'pc' => $this->pc?->name, 'rule_id' => $this->rule_id,
            'severity' => $this->severity, 'title' => $this->title, 'user' => $this->user,
            'event_class' => $meta['event_class'], 'confidence' => $meta['confidence'],
            'evidence_meta' => $meta,
            'occurred_at' => $this->occurred_at?->toIso8601String(), 'session_id' => $this->lab_session_id,
        ];
    }
}
