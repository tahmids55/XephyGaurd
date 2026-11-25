<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use LogicException;

/** Append-only review of an alert. Updates and deletes are refused at the model level. */
class AlertReview extends Model
{
    public $timestamps = false;

    protected $fillable = ['alert_id', 'user_id', 'note', 'reviewed_at'];

    protected $casts = ['reviewed_at' => 'datetime'];

    protected static function booted(): void
    {
        static::updating(fn () => throw new LogicException('alert reviews are append-only'));
        static::deleting(fn () => throw new LogicException('alert reviews are append-only'));
    }

    public function alert(): BelongsTo
    {
        return $this->belongsTo(Alert::class);
    }

    public function user(): BelongsTo
    {
        return $this->belongsTo(User::class);
    }
}
