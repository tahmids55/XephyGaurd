<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class StudentPcAssignment extends Model
{
    public $timestamps = false;
    protected $fillable = ['student_id', 'pc_id', 'lab_session_id', 'first_seen_at', 'last_seen_at'];
    protected $casts = ['first_seen_at' => 'datetime', 'last_seen_at' => 'datetime'];
}
