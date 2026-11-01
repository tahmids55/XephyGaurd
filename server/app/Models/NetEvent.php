<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class NetEvent extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'lab_session_id', 'client_ip', 'host', 'path', 'layer', 'action', 'category', 'matched', 'occurred_at'];
    protected $casts = ['occurred_at' => 'datetime'];
}
