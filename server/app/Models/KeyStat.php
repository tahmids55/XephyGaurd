<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;

class KeyStat extends Model
{
    public $timestamps = false;
    protected $fillable = ['pc_id', 'lab_session_id', 'user', 'window_start', 'window_s', 'keys', 'backspaces', 'iki_mean',
        'iki_median', 'iki_p90', 'iki_std', 'bursts', 'max_pause', 'paste_chords'];
    protected $casts = ['window_start' => 'datetime'];
}
