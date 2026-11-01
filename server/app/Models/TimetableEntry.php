<?php

namespace App\Models;

use Carbon\CarbonInterface;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;

class TimetableEntry extends Model
{
    protected $fillable = ['lab_id', 'profile_id', 'course', 'weekday', 'start_time', 'end_time', 'valid_from', 'valid_until', 'enabled'];
    protected $casts = ['enabled' => 'boolean', 'valid_from' => 'date', 'valid_until' => 'date'];

    public function profile(): BelongsTo { return $this->belongsTo(Profile::class); }
    public function lab(): BelongsTo { return $this->belongsTo(Lab::class); }

    /** True if this weekly slot covers the given moment. */
    public function covers(CarbonInterface $t): bool
    {
        if (! $this->enabled || $t->dayOfWeekIso !== (int) $this->weekday) {
            return false;
        }
        if ($this->valid_from && $t->lt($this->valid_from->startOfDay())) {
            return false;
        }
        if ($this->valid_until && $t->gt($this->valid_until->endOfDay())) {
            return false;
        }
        $hm = $t->format('H:i:s');

        return $hm >= $this->start_time && $hm < $this->end_time;
    }
}
