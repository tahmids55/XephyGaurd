<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class Consent extends Model
{
    protected $fillable = ['student_id', 'purpose', 'granted', 'form_version', 'recorded_by', 'recorded_at'];
    protected $casts = ['granted' => 'boolean', 'recorded_at' => 'datetime'];
}
