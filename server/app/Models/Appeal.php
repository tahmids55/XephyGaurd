<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Appeal extends Model
{
    public const STATUSES = ['open', 'upheld', 'overturned', 'noted'];

    protected $fillable = [
        'lab_session_id',
        'student_id',
        'score_id',
        'alert_id',
        'status',
        'student_statement',
        'evidence_snapshot',
        'decision_reason',
        'decided_by',
        'decided_at',
    ];

    protected $casts = [
        'evidence_snapshot' => 'array',
        'decided_at' => 'datetime',
    ];

    public function session(): BelongsTo
    {
        return $this->belongsTo(LabSession::class, 'lab_session_id');
    }

    public function student(): BelongsTo
    {
        return $this->belongsTo(Student::class);
    }

    public function score(): BelongsTo
    {
        return $this->belongsTo(Score::class);
    }

    public function alert(): BelongsTo
    {
        return $this->belongsTo(Alert::class);
    }

    public function decider(): BelongsTo
    {
        return $this->belongsTo(User::class, 'decided_by');
    }
}
