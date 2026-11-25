<?php

declare(strict_types=1);

namespace App\Observers;

use App\Models\Alert;
use App\Models\AlertSnapshot;
use App\Models\Event;
use App\Models\NetEvent;
use Throwable;

/**
 * F123 / DECISIONS #32: on every new alert, freeze a copy of data the CCC already holds about the PC.
 * Runs synchronously right after the insert (no queue) so the context is the one at alert time.
 * A failure is reported and never blocks the alert itself. No new data is collected here.
 */
final class AlertSnapshotRecorder
{
    public const WINDOW_S = 120;

    public const MAX_EVENTS = 20;

    public const MAX_PROCESSES = 250;

    private const SKIP = ['key_stats', 'heartbeat', 'process_snapshot', 'extension_scan', 'artifact_scan'];

    public function created(Alert $alert): void
    {
        try {
            $payload = json_encode($this->context($alert), JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_THROW_ON_ERROR);
            AlertSnapshot::create(['alert_id' => $alert->id, 'pc_id' => $alert->pc_id, 'captured_at' => now(),
                'payload' => $payload, 'sha256' => hash('sha256', $payload)]);
        } catch (Throwable $e) {
            report($e);
        }
    }

    /** @return array<string, mixed> */
    private function context(Alert $alert): array
    {
        $at = $alert->occurred_at ?? now();
        $from = $at->copy()->subSeconds(self::WINDOW_S);
        $pc = $alert->pc;

        $snap = Event::query()->where('pc_id', $alert->pc_id)->where('type', 'process_snapshot')
            ->orderByDesc('seq')->first(['occurred_at', 'data']);
        $events = Event::query()->where('pc_id', $alert->pc_id)->whereNotIn('type', self::SKIP)
            ->whereBetween('occurred_at', [$from, $at])->orderByDesc('occurred_at')->limit(self::MAX_EVENTS)
            ->get(['type', 'occurred_at', 'user', 'data']);
        $net = NetEvent::query()->where('pc_id', $alert->pc_id)->whereBetween('occurred_at', [$from, $at])
            ->orderByDesc('occurred_at')->limit(self::MAX_EVENTS)
            ->get(['host', 'path', 'layer', 'action', 'category', 'matched', 'occurred_at']);

        return [
            'v' => 1,
            'alert' => ['id' => $alert->id, 'rule_id' => $alert->rule_id, 'severity' => $alert->severity,
                'category' => $alert->category, 'title' => $alert->title, 'details' => $alert->details,
                'user' => $alert->user, 'occurred_at' => $at->toIso8601ZuluString('millisecond')],
            'lab_session_id' => $alert->lab_session_id,
            'pc' => $pc ? ['id' => $pc->id, 'name' => $pc->name, 'ip' => $pc->ip, 'mode' => $pc->mode,
                'user' => $pc->current_user, 'active_app' => $pc->active_app, 'isolated' => (bool) $pc->isolated,
                'live_score' => $pc->live_score] : null,
            'processes' => $snap ? ['at' => $snap->occurred_at?->toIso8601ZuluString('millisecond'),
                'list' => array_slice((array) ($snap->data['list'] ?? []), 0, self::MAX_PROCESSES)] : null,
            'events' => $events->map(fn ($e) => ['type' => $e->type, 'at' => $e->occurred_at?->toIso8601ZuluString('millisecond'),
                'user' => $e->user, 'data' => $e->data])->all(),
            'net' => $net->map(fn ($n) => ['host' => $n->host, 'path' => $n->path, 'layer' => $n->layer, 'action' => $n->action,
                'category' => $n->category, 'matched' => $n->matched, 'at' => $n->occurred_at?->toIso8601ZuluString('millisecond')])->all(),
        ];
    }
}
