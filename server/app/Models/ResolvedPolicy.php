<?php

declare(strict_types=1);

namespace App\Models;

use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Facades\DB;

/** Cached result of PolicyService for one PC (S-23). Stale once the global generation moves on. */
class ResolvedPolicy extends Model
{
    protected $fillable = ['pc_id', 'generation', 'stamp', 'version', 'policy'];

    protected $casts = ['policy' => 'array', 'generation' => 'integer', 'version' => 'integer'];

    public static function generation(): int
    {
        return (int) (DB::table('policy_generation')->where('id', 1)->value('generation') ?? 0);
    }

    /** Called on every change of a policy input (profile, rule, session, override, setting, signatures). */
    public static function invalidateAll(): void
    {
        if (DB::table('policy_generation')->where('id', 1)->increment('generation') === 0) {
            DB::table('policy_generation')->insertOrIgnore(['id' => 1, 'generation' => 2]);
        }
    }
}
