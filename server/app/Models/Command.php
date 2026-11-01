<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Command extends Model
{
    protected $fillable = ['pc_id', 'user_id', 'type', 'args', 'status', 'result', 'acked_at'];
    protected $casts = ['args' => 'array', 'result' => 'array', 'acked_at' => 'datetime'];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
}
