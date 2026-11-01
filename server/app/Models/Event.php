<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Event extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'agent_id', 'seq', 'lab_session_id', 'type', 'occurred_at', 'received_at', 'user', 'data'];
    protected $casts = ['data' => 'array', 'occurred_at' => 'datetime', 'received_at' => 'datetime'];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
}
