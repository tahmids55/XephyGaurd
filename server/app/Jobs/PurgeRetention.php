<?php

namespace App\Jobs;

use App\Models\Event;
use App\Models\KeyStat;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Report;
use App\Models\Setting;
use App\Services\Audit;
use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Foundation\Queue\Queueable;

/** Deletes raw events after the configured retention; aggregated scores/reports live longer. */
class PurgeRetention implements ShouldQueue
{
    use Queueable;

    public function handle(): void
    {
        $raw = now()->subDays((int) Setting::get('retention_days_raw'));
        $counts = [
            'events' => Event::query()->where('occurred_at', '<', $raw)->delete(),
            'key_stats' => KeyStat::query()->where('window_start', '<', $raw)->delete(),
            'paste_events' => PasteEvent::query()->where('occurred_at', '<', $raw)->delete(),
            'net_events' => NetEvent::query()->where('occurred_at', '<', $raw)->delete(),
            'reports' => Report::query()->where('created_at', '<', now()->subDays((int) Setting::get('retention_days_reports')))->delete(),
        ];
        if (array_sum($counts) > 0) {
            Audit::log('retention.purge', null, $counts);
        }
    }
}
