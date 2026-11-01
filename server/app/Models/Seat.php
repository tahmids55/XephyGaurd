<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Database\Eloquent\Relations\HasOne;

class Seat extends Model
{
    protected $fillable = ['lab_id', 'label', 'x', 'y'];

    public function lab(): BelongsTo { return $this->belongsTo(Lab::class); }
    public function pc(): HasOne { return $this->hasOne(Pc::class); }
}
