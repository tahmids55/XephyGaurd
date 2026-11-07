<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class NetEvent extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'lab_session_id', 'client_ip', 'host', 'path', 'layer', 'action', 'category', 'matched', 'occurred_at'];
    protected $casts = ['occurred_at' => 'datetime'];
    protected $appends = ['event_class', 'confidence', 'evidence_meta'];

    public function getEvidenceMetaAttribute(): array
    {
        $isBlocked = $this->action === 'blocked';

        return [
            'event_class' => $isBlocked ? 'blocked' : 'system',
            'confidence' => 'strong',
            'what_it_means' => $isBlocked
                ? 'A network request or DNS lookup to a blocked domain was intercepted and blocked at the lab gateway'
                : 'Allowed or rewritten gateway network event',
            'what_it_does_not_mean' => $isBlocked
                ? 'The request was blocked before completion; background tabs, extensions or prefetchers can also trigger blocked lookups'
                : 'Normal allowed network traffic',
        ];
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
