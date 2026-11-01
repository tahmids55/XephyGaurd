<?php

use App\Jobs\PurgeRetention;
use App\Services\SessionManager;
use Illuminate\Support\Facades\Schedule;

// Timetable: start/stop sessions on schedule.
Schedule::call(fn () => app(SessionManager::class)->timetableTick())->everyMinute()->name('xg-timetable')->withoutOverlapping();
// "Machine silent" detection (3 missed 5 s heartbeats).
Schedule::call(fn () => app(SessionManager::class)->detectSilent())->everyFiveSeconds()->name('xg-silent')->withoutOverlapping();
// Retention purge of raw events.
Schedule::job(new PurgeRetention)->dailyAt('03:15');
