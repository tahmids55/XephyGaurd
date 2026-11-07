<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class TwoPersonRequest extends Model
{
    public const STATUSES = ['pending', 'approved', 'rejected'];

    protected $fillable = [
        'action',
        'target_type',
        'target_id',
        'payload',
        'reason',
        'requested_by',
        'approved_by',
        'status',
        'decided_at',
    ];

    protected $casts = [
        'payload' => 'array',
        'decided_at' => 'datetime',
    ];

    public function requester(): BelongsTo
    {
        return $this->belongsTo(User::class, 'requested_by');
    }

    public function approver(): BelongsTo
    {
        return $this->belongsTo(User::class, 'approved_by');
    }
}
