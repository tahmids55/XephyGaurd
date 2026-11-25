<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

/** Instructor-captured screenshot evidence linked to a PC and optional lab session. */
class EvidenceCapture extends Model
{
    protected $fillable = [
        'pc_id',
        'lab_session_id',
        'user',
        'captured_by',
        'screen_view_audit_id',
        'monitor',
        'source_mode',
        'file_path',
        'mime_type',
        'sha256',
        'size_bytes',
        'encrypted',
        'note',
        'occurred_at',
    ];

    protected $casts = [
        'monitor' => 'integer',
        'size_bytes' => 'integer',
        'encrypted' => 'boolean',
        'occurred_at' => 'datetime',
    ];

    public function pc(): BelongsTo
    {
        return $this->belongsTo(Pc::class);
    }

    public function session(): BelongsTo
    {
        return $this->belongsTo(LabSession::class, 'lab_session_id');
    }

    public function capturer(): BelongsTo
    {
        return $this->belongsTo(User::class, 'captured_by');
    }

    public function toApiArray(): array
    {
        return [
            'id' => $this->id,
            'pc_id' => $this->pc_id,
            'lab_session_id' => $this->lab_session_id,
            'user' => $this->user,
            'captured_by' => $this->relationLoaded('capturer') ? $this->capturer?->name : null,
            'monitor' => $this->monitor,
            'source_mode' => $this->source_mode,
            'mime_type' => $this->mime_type,
            'sha256' => $this->sha256,
            'size_bytes' => $this->size_bytes,
            'note' => $this->note,
            'occurred_at' => $this->occurred_at?->format('H:i:s'),
            'occurred_iso' => $this->occurred_at?->toIso8601String(),
            'url' => route('evidence.show', $this),
        ];
    }
}
