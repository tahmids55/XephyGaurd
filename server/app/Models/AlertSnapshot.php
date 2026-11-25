<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

/** F123: what the CCC knew about a PC at the moment an alert was raised (DECISIONS #32). */
class AlertSnapshot extends Model
{
    public $timestamps = false;

    protected $fillable = ['alert_id', 'pc_id', 'captured_at', 'payload', 'sha256'];

    protected $casts = ['captured_at' => 'datetime'];

    public function alert(): BelongsTo
    {
        return $this->belongsTo(Alert::class);
    }

    /** @return array<string, mixed> */
    public function data(): array
    {
        return (array) json_decode($this->payload, true);
    }

    /** The stored text still hashes to the stored sha256 (not edited since capture). */
    public function intact(): bool
    {
        return hash_equals($this->sha256, hash('sha256', $this->payload));
    }
}
