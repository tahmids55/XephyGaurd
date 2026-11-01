<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Report extends Model
{
    protected $fillable = ['lab_session_id', 'student_id', 'kind', 'status', 'summary', 'path', 'requested_by'];
    protected $casts = ['summary' => 'array'];
}
