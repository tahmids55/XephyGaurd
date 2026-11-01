<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Score extends Model
{
    protected $fillable = ['lab_session_id', 'pc_id', 'student_id', 'user', 'method', 'score', 'confidence', 'reasons', 'features', 'not_observable'];
    protected $casts = ['reasons' => 'array', 'features' => 'array', 'not_observable' => 'array'];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
    public function student(): BelongsTo { return $this->belongsTo(Student::class); }
}
