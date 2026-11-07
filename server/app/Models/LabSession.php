<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Builder;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Database\Eloquent\Relations\BelongsToMany;
use Illuminate\Database\Eloquent\Relations\HasMany;

/** A lab session: course + mode + profile + roster + time window. */
class LabSession extends Model
{
    protected $table = 'lab_sessions';
    protected $fillable = ['lab_id', 'profile_id', 'timetable_entry_id', 'course', 'title', 'mode', 'status',
        'starts_at', 'ends_at', 'started_at', 'ended_at', 'started_by', 'experiment_condition', 'experiment_group'];
    protected $casts = ['starts_at' => 'datetime', 'ends_at' => 'datetime', 'started_at' => 'datetime', 'ended_at' => 'datetime'];

    public function lab(): BelongsTo { return $this->belongsTo(Lab::class); }
    public function profile(): BelongsTo { return $this->belongsTo(Profile::class); }
    public function pcs(): BelongsToMany
    {
        return $this->belongsToMany(Pc::class, 'session_pcs')->withPivot(['override_profile_id', 'override_reason', 'student_id'])->withTimestamps();
    }
    public function sessionPcs(): HasMany { return $this->hasMany(SessionPc::class); }
    public function alerts(): HasMany { return $this->hasMany(Alert::class); }
    public function scores(): HasMany { return $this->hasMany(Score::class); }

    public function scopeActive(Builder $q): Builder { return $q->where('status', 'active'); }

    public static function activeFor(int $labId): ?self
    {
        return static::query()->where('lab_id', $labId)->active()->latest('started_at')->first();
    }
}
