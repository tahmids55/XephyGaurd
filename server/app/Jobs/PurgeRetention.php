<?php

declare(strict_types=1);

namespace App\Jobs;

use App\Services\Audit;
use App\Services\RetentionRegistry;
use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Foundation\Queue\Queueable;
use Illuminate\Support\Facades\Storage;

/** Deletes every table listed in RetentionRegistry once it is older than its class's retention. */
class PurgeRetention implements ShouldQueue
{
    use Queueable;

    public function handle(): void
    {
        $counts = [];
        foreach (RetentionRegistry::entries() as $table => [$model, $column, $class, $fileColumn]) {
            $query = $model::query()->where($column, '<', RetentionRegistry::cutoff($class));
            if ($fileColumn !== null) {
                $expired = (clone $query)->get(['id', $fileColumn]);
                foreach ($expired as $row) {
                    Storage::disk('local')->delete($row->{$fileColumn});
                }
                $query = $model::query()->whereIn('id', $expired->pluck('id'));
            }
            $counts[$table] = $query->delete();
        }
        if (array_sum($counts) > 0) {
            Audit::log('retention.purge', null, $counts);
        }
    }
}
