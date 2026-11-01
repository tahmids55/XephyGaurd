<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use LogicException;

/** Append-only audit trail. Updates and deletes are refused at the model level. */
class AuditLog extends Model
{
    protected $table = 'audit_log';
    public const UPDATED_AT = null;
    protected $fillable = ['user_id', 'action', 'subject_type', 'subject_id', 'details', 'ip'];
    protected $casts = ['details' => 'array'];

    protected static function booted(): void
    {
        static::updating(fn () => throw new LogicException('audit log is append-only'));
        static::deleting(fn () => throw new LogicException('audit log is append-only'));
    }

    public function user(): BelongsTo { return $this->belongsTo(User::class); }
}
