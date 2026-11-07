<?php

namespace App\Jobs;

use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Foundation\Queue\Queueable;
use Illuminate\Support\Facades\Artisan;

/** Deletes raw events after the configured retention; aggregated scores/reports live longer. */
class PurgeRetention implements ShouldQueue
{
    use Queueable;

    public function handle(): void
    {
        Artisan::call('xephyguard:purge');
    }
}
