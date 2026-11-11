<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class ScreenViewAudit extends Model
{
    protected $table = 'screen_view_audits';

    protected $fillable = [
        'instructor_id',
        'student_id',
        'lab_session_id',
        'stream_id',
        'mode',
        'started_at',
        'ended_at',
    ];

    protected $casts = [
        'started_at' => 'datetime',
        'ended_at' => 'datetime',
        'stream_id' => 'integer',
    ];

    public function instructor(): BelongsTo
    {
        return $this->belongsTo(User::class, 'instructor_id');
    }

    public function student(): BelongsTo
    {
        return $this->belongsTo(Student::class, 'student_id');
    }

    public function labSession(): BelongsTo
    {
        return $this->belongsTo(LabSession::class, 'lab_session_id');
    }
}
