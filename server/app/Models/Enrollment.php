<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class Enrollment extends Model
{
    protected $fillable = ['lab_id', 'hostname', 'os', 'os_version', 'macs', 'ip', 'agent_version', 'csr_pem',
        'poll_token_hash', 'status', 'pc_id', 'cert_pem', 'issued_token', 'decided_by', 'decided_at'];
    protected $hidden = ['csr_pem', 'poll_token_hash', 'issued_token'];
    protected $casts = ['macs' => 'array', 'decided_at' => 'datetime', 'issued_token' => 'encrypted'];

    public function lab(): BelongsTo { return $this->belongsTo(Lab::class); }
    public function pc(): BelongsTo { return $this->belongsTo(Pc::class); }
}
