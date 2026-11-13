<?php

use App\Jobs\PurgeRetention;
use App\Services\SessionManager;
use Illuminate\Support\Facades\Schedule;

// Timetable: start/stop sessions on schedule.
Schedule::call(fn () => app(SessionManager::class)->timetableTick())->everyMinute()->name('xg-timetable')->withoutOverlapping();
// "Machine silent" detection (3 missed 5 s heartbeats).
Schedule::call(fn () => app(SessionManager::class)->detectSilent())->everyFiveSeconds()->name('xg-silent')->withoutOverlapping();
// Auto-publish signatures: any rule/blocklist change is compiled within a minute (debounced by the schedule
// interval); publish() is a no-op when the content hash is unchanged and refuses bundles without AI/IDE domains.
Schedule::call(function () {
    if (! \App\Services\CertificateAuthority::fromConfig()->exists()) {
        return;
    }
    try {
        app(\App\Services\SignatureCompiler::class)->publish(null, 'auto');
    } catch (\RuntimeException $e) {
        \Illuminate\Support\Facades\Log::warning('signatures auto-publish skipped: '.$e->getMessage());
    }
})->everyMinute()->name('xg-signatures-autopublish')->withoutOverlapping();
// Retention purge of raw events.
Schedule::job(new PurgeRetention)->dailyAt('03:15');
