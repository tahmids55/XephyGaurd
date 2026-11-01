<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class PasteEvent extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'lab_session_id', 'user', 'is_copy', 'chars', 'lines', 'looks_like_code', 'code_score',
        'source_app', 'target_app', 'occurred_at'];
    protected $casts = ['is_copy' => 'boolean', 'looks_like_code' => 'boolean', 'occurred_at' => 'datetime'];
}
