<?php

namespace App\Models;

use App\Services\EventIngestor;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Event extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'agent_id', 'seq', 'lab_session_id', 'type', 'occurred_at', 'received_at', 'user', 'data'];
    protected $casts = ['data' => 'array', 'occurred_at' => 'datetime', 'received_at' => 'datetime'];
    protected $appends = ['evidence_meta', 'event_class', 'confidence'];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }

    public function getEvidenceMetaAttribute(): array
    {
        $d = is_array($this->data) ? $this->data : [];
        if (isset($d['evidence_meta']) && is_array($d['evidence_meta'])) {
            return $d['evidence_meta'];
        }

        return EventIngestor::classifyEvidence((string) $this->type, $d);
    }

    public function getEventClassAttribute(): string
    {
        return $this->evidence_meta['event_class'];
    }

    public function getConfidenceAttribute(): string
    {
        return $this->evidence_meta['confidence'];
    }
}
