<?php

declare(strict_types=1);

namespace App\Observers;

use App\Models\Alert;
use App\Services\CommandBus;
use App\Services\PrivacySettings;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\DB;
use Throwable;

/**
 * F123 / DECISIONS #32: a serious alert asks its PC for one screenshot at once (command `evidence_snapshot`,
 * pushed like every command). Only when an admin enabled it, never in Always-on, at most once per PC per
 * RATE_S. Sent after the commit so the command row (= the upload's nonce record) exists when the agent answers.
 */
final class AutoScreenshotRequester
{
    public const RATE_S = 30;

    public function created(Alert $alert): void
    {
        try {
            $cfg = PrivacySettings::autoScreenshot();
            $pc = $alert->pc;
            if (! $cfg['enabled'] || $pc === null || $pc->mode === 'always_on'
                || (Alert::SEVERITY_POINTS[$alert->severity] ?? -1) < Alert::SEVERITY_POINTS[$cfg['min_severity']]
                || ! Cache::add("evidence:auto:pc:{$pc->id}", 1, self::RATE_S)) {
                return;
            }
            $args = ['alert_id' => $alert->id, 'nonce' => bin2hex(random_bytes(16))];
            DB::afterCommit(function () use ($pc, $args) {
                try {
                    app(CommandBus::class)->send($pc, 'evidence_snapshot', $args);
                } catch (Throwable $e) {
                    report($e);
                }
            });
        } catch (Throwable $e) {
            report($e);
        }
    }
}
