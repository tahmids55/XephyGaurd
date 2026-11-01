<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Agent extends Model
{
    protected $fillable = ['pc_id', 'token_hash', 'cert_serial', 'cert_fingerprint', 'cert_expires_at', 'version',
        'last_seq', 'clock_offset_ms', 'policy_version', 'signatures_version', 'revoked_at'];
    protected $hidden = ['token_hash'];
    protected $casts = ['cert_expires_at' => 'datetime', 'revoked_at' => 'datetime'];

    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
}
