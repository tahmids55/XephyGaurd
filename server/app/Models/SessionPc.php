<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class SessionPc extends Model
{
    protected $table = 'session_pcs';
    protected $fillable = ['lab_session_id', 'pc_id', 'override_profile_id', 'override_reason', 'student_id'];

    public function session(): BelongsTo { return $this->belongsTo(LabSession::class, 'lab_session_id'); }
    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
    public function overrideProfile(): BelongsTo { return $this->belongsTo(Profile::class, 'override_profile_id'); }
}
