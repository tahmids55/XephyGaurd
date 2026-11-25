<?php

declare(strict_types=1);

namespace App\Models\Concerns;

use App\Models\ResolvedPolicy;

/** Any save or delete of a model that feeds the agent policy makes every cached policy stale (S-23). */
trait InvalidatesPolicies
{
    public static function bootInvalidatesPolicies(): void
    {
        static::saved(fn () => ResolvedPolicy::invalidateAll());
        static::deleted(fn () => ResolvedPolicy::invalidateAll());
    }
}
