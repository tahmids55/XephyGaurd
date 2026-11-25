<?php

declare(strict_types=1);

namespace App\View\Composers;

use App\Models\Alert;
use Illuminate\View\View;

/** Gives the app layout its unacknowledged-alert count (rail badge). */
class OpenAlertsComposer
{
    public function compose(View $view): void
    {
        try {
            $count = Alert::query()->whereNull('acknowledged_at')->count();
        } catch (\Throwable) {
            $count = 0;
        }

        $view->with('xgOpenAlerts', $count);
    }
}
