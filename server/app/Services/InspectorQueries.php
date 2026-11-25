<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Alert;
use App\Models\Event;
use App\Models\EvidenceCapture;
use App\Models\KeyStat;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Pc;
use App\Models\SessionPc;
use App\Models\Student;

/** Inspector (PC detail drawer) data: timeline, processes, network log, key-rate chart, paste log. */
class InspectorQueries
{
    /** $session = the lab's active session (or null); $withEvidence = caller may see evidence (instructor+). */
    public function forPc(Pc $pc, ?LabSession $session, bool $withEvidence): array
    {
        $since = $session?->started_at ?? now()->subHours(24);
        $events = Event::query()->where('pc_id', $pc->id)->where('occurred_at', '>=', $since)
            ->whereNotIn('type', ['key_stats', 'heartbeat', 'process_snapshot', 'extension_scan', 'artifact_scan'])->latest('occurred_at')->limit(200)->get(['type', 'occurred_at', 'data', 'user'])
            ->reject(fn ($e) => $e->type === 'rule_hit' && (
                ($e->data['category'] ?? '') === 'extension'
                && empty($e->data['context']['activity'] ?? '')
                && ($e->data['mode'] ?? '') !== 'test'
            ))
            ->values();
        $latestSnap = Event::query()->where('pc_id', $pc->id)->where('type', 'process_snapshot')->latest('seq')->first(['occurred_at', 'data']);
        $latestExt = Event::query()->where('pc_id', $pc->id)->where('type', 'extension_scan')->latest('seq')->first(['occurred_at', 'data']);
        $latestArt = Event::query()->where('pc_id', $pc->id)->where('type', 'artifact_scan')->latest('seq')->first(['occurred_at', 'data']);
        $keys = KeyStat::query()->where('pc_id', $pc->id)->where('window_start', '>=', $since)->orderBy('window_start')
            ->get(['window_start', 'window_s', 'keys', 'backspaces', 'paste_chords']);
        // Bucket per minute for the key-rate chart.
        $perMinute = $keys->groupBy(fn ($k) => $k->window_start->format('H:i'))->map(fn ($g) => [
            'keys' => $g->sum('keys'), 'backspaces' => $g->sum('backspaces'), 'pastes' => $g->sum('paste_chords'),
        ]);

        $sessionPc = $session ? SessionPc::where('lab_session_id', $session->id)->where('pc_id', $pc->id)->first() : null;
        $student = $sessionPc?->student_id ? Student::find($sessionPc->student_id) : Student::forLogin($pc->current_user);
        $netEvents = NetEvent::query()->where('pc_id', $pc->id)->where('occurred_at', '>=', $since)->latest('occurred_at')
            ->limit(100)->get(['host', 'path', 'layer', 'action', 'category', 'matched', 'occurred_at']);

        $agentAiTraffic = $events->where('type', 'rule_hit')
            ->filter(fn ($e) => ! empty($e->data['context']['domain'] ?? ''))
            ->values()
            ->map(fn ($e) => [
                'at' => $e->occurred_at->format('H:i:s'),
                'rule_id' => $e->data['rule_id'] ?? '',
                'description' => $e->data['description'] ?? '',
                'process' => $e->data['context']['process'] ?? ($e->data['context']['ide'] ?? ''),
                'domain' => $e->data['context']['domain'] ?? '',
                'remote_ip' => $e->data['context']['remote_ip'] ?? '',
                'sent_kb' => $e->data['context']['sent_kb'] ?? 0,
                'recv_kb' => $e->data['context']['recv_kb'] ?? 0,
                'count' => 1,
                'mode' => 'Observed',
            ]);

        $proxyAiTraffic = $netEvents
            ->filter(fn ($n) => in_array($n->action, ['observed', 'blocked'], true)
                && in_array($n->category, ['ai', 'ide', 'search_ai'], true))
            ->groupBy(fn ($n) => ($n->host ?: $n->matched ?: '').'|'.$n->action)
            ->values()
            ->map(function ($group) {
                $first = $group->first();

                return [
                    'at' => $first->occurred_at?->format('H:i:s'),
                    'rule_id' => 'proxy.'.($first->category ?? 'ai'),
                    'description' => match ($first->category) {
                        'ide' => 'IDE AI backend',
                        'search_ai' => 'Search AI',
                        default => 'AI web service',
                    },
                    'process' => 'proxy ('.$first->layer.')',
                    'domain' => $first->host ?: ($first->matched ?? ''),
                    'remote_ip' => '',
                    'sent_kb' => 0,
                    'recv_kb' => 0,
                    'count' => $group->count(),
                    'mode' => $first->action === 'blocked' ? 'Blocked' : 'Observed',
                ];
            });

        return [
            'pc' => $pc->tile() + ['hostname' => $pc->hostname, 'os_version' => $pc->os_version, 'ip' => $pc->ip,
                'capabilities' => $pc->capabilities, 'usage' => $pc->usage, 'agent_version' => $pc->agent?->version,
                'screen_cost' => $pc->screenCostLabel()],
            'student' => $student ? ['id' => $student->id, 'name' => $student->name, 'login' => $student->login] : null,
            'alerts' => Alert::query()->where('pc_id', $pc->id)->where('occurred_at', '>=', $since)->latest('occurred_at')
                ->limit(50)->get(['id', 'rule_id', 'severity', 'title', 'occurred_at', 'acknowledged_at'])
                ->map(fn ($a) => [
                    'id' => $a->id, 'rule_id' => $a->rule_id, 'severity' => $a->severity, 'title' => $a->title,
                    'occurred_at' => $a->occurred_at?->format('H:i:s'),
                    'acknowledged_at' => $a->acknowledged_at?->format('H:i:s'),
                ]),
            'timeline' => $events->map(fn ($e) => ['type' => $e->type, 'at' => $e->occurred_at->format('H:i:s'),
                'summary' => self::summarize($e->type, $e->data), 'user' => $e->user]),
            'running_processes' => $latestSnap?->data['list'] ?? [],
            'processes_updated_at' => $latestSnap?->occurred_at?->format('H:i:s'),
            'extensions' => $latestExt?->data['ai'] ?? [],
            'extensions_total' => $latestExt?->data['count'] ?? 0,
            'extensions_updated_at' => $latestExt?->occurred_at?->format('H:i:s'),
            'artifacts' => $latestArt?->data['found'] ?? [],
            'artifacts_visited' => $latestArt?->data['files_visited'] ?? 0,
            'artifacts_updated_at' => $latestArt?->occurred_at?->format('H:i:s'),
            'browser_tabs' => Event::query()->where('pc_id', $pc->id)->where('type', 'window_focus')
                ->where('occurred_at', '>=', $since)->latest('occurred_at')
                ->limit(60)->get(['occurred_at', 'user', 'data'])->map(fn ($e) => [
                    'at' => $e->occurred_at->format('H:i:s'),
                    'app' => $e->data['app'] ?? '',
                    'title' => $e->data['title'] ?? '',
                    'user' => $e->user ?: ($e->data['user'] ?? ''),
                    'ai_site' => $e->data['ai_site'] ?? '',
                ]),
            'ai_traffic' => $agentAiTraffic->concat($proxyAiTraffic)->sortByDesc('at')->values(),
            'processes' => $events->whereIn('type', ['process_start', 'process_stop'])->values()
                ->map(fn ($e) => ['at' => $e->occurred_at->format('H:i:s'), 'type' => $e->type, 'pid' => $e->data['pid'] ?? null, 'exe' => $e->data['exe'] ?? '', 'rule' => $e->data['rule_id'] ?? '']),
            'adapters' => Event::query()->where('pc_id', $pc->id)->where('type', 'net_adapter')->latest('occurred_at')
                ->limit(30)->get(['occurred_at', 'data'])->map(fn ($e) => [
                    'at' => $e->occurred_at->format('H:i:s'),
                    'change' => $e->data['change'] ?? '',
                    'name' => $e->data['name'] ?? '',
                    'description' => $e->data['description'] ?? '',
                    'driver' => $e->data['driver'] ?? '',
                    'addresses' => $e->data['addresses'] ?? [],
                    'up' => (bool) ($e->data['up'] ?? false),
                    'wireless' => (bool) ($e->data['wireless'] ?? false),
                    'default_route' => (bool) ($e->data['default_route'] ?? false),
                    'kind' => $e->data['kind'] ?? '',
                ]),
            'listening_ports' => $events->where('type', 'listening_port')->values()
                ->map(fn ($e) => [
                    'at' => $e->occurred_at->format('H:i:s'),
                    'port' => $e->data['port'] ?? 0,
                    'address' => $e->data['address'] ?? '',
                    'process' => $e->data['process'] ?? '',
                    'pid' => $e->data['pid'] ?? null,
                    'rule_id' => $e->data['rule_id'] ?? '',
                    'probed' => (bool) ($e->data['probed'] ?? false),
                ]),
            'devices' => $events->where('type', 'device')->values()
                ->map(fn ($e) => [
                    'at' => $e->occurred_at->format('H:i:s'),
                    'action' => $e->data['action'] ?? '',
                    'class' => $e->data['class'] ?? '',
                    'vendor' => $e->data['vendor'] ?? '',
                    'product' => $e->data['product'] ?? '',
                    'model' => $e->data['model'] ?? '',
                    'rule_id' => $e->data['rule_id'] ?? '',
                ]),
            'network' => $netEvents->map(fn ($n) => [
                'host' => $n->host, 'path' => $n->path, 'layer' => $n->layer, 'action' => $n->action,
                'category' => $n->category, 'occurred_at' => $n->occurred_at?->format('H:i:s'),
            ]),
            'pastes' => PasteEvent::query()->where('pc_id', $pc->id)->where('occurred_at', '>=', $since)->latest('occurred_at')
                ->limit(100)->get(['is_copy', 'chars', 'lines', 'looks_like_code', 'source_app', 'target_app', 'occurred_at'])
                ->map(fn ($p) => [
                    'is_copy' => $p->is_copy, 'chars' => $p->chars, 'lines' => $p->lines,
                    'looks_like_code' => $p->looks_like_code, 'source_app' => $p->source_app,
                    'target_app' => $p->target_app, 'occurred_at' => $p->occurred_at?->format('H:i:s'),
                ]),
            'keyrate' => ['labels' => $perMinute->keys()->values(), 'series' => $perMinute->values()],
            'evidence' => ! $withEvidence ? [] : EvidenceCapture::query()->with('capturer:id,name')->where('pc_id', $pc->id)
                ->where('occurred_at', '>=', $since)->latest('occurred_at')->limit(20)
                ->get()->map->toApiArray()->values(),
        ];
    }

    public static function summarize(string $type, array $d): string
    {
        return match ($type) {
            'rule_hit' => ($d['description'] ?? $d['rule_id'] ?? '').' — '.($d['subject'] ?? '').' ['.implode(',', $d['actions'] ?? []).']',
            'process_start' => 'started '.($d['exe'] ?? '?'),
            'process_stop' => 'stopped pid '.($d['pid'] ?? '?'),
            'window_focus' => 'focus '.($d['app'] ?? '').(($d['title'] ?? '') && ($d['title'] ?? '') !== ($d['app'] ?? '') ? ': '.$d['title'] : ''),
            'paste' => (! empty($d['src']) || ! empty($d['dst']))
                ? ($d['src'] ?: '?').' → '.($d['dst'] ?: '?').': paste '.($d['chars'] ?? 0).' chars / '.($d['lines'] ?? 0).' lines'.(($d['code'] ?? false) ? ' (code)' : '')
                : 'paste '.($d['chars'] ?? 0).' chars / '.($d['lines'] ?? 0).' lines'.(($d['code'] ?? false) ? ' (code)' : ''),
            'copy' => (! empty($d['src']) ? ($d['src'].': ') : '')
                .'copy '.($d['chars'] ?? 0).' chars / '.($d['lines'] ?? 0).' lines'.(($d['code'] ?? false) ? ' (code)' : ''),
            'listening_port' => 'port '.($d['address'] ?? '127.0.0.1').':'.($d['port'] ?? '?').' ('.($d['process'] ?? '?').') ['.($d['rule_id'] ?? '').']',
            'net_adapter' => ($d['change'] ?? '').' adapter '.($d['name'] ?? '').(! empty($d['description']) ? ' ('.$d['description'].')' : (! empty($d['driver']) ? ' ('.$d['driver'].')' : '')).(! empty($d['kind']) ? ' ['.$d['kind'].']' : ''),
            'device' => ($d['action'] ?? '').' '.($d['class'] ?? 'usb').' '.(($d['model'] ?? '') ?: (($d['vendor'] ?? '') ? ($d['vendor'].':'.($d['product'] ?? '')) : '')).(! empty($d['rule_id']) ? ' ['.$d['rule_id'].']' : ''),
            'tamper' => 'TAMPER '.($d['kind'] ?? ''),
            'extension_scan' => ($d['count'] ?? 0).' extensions, AI: '.count($d['ai'] ?? []),
            'artifact_scan' => ($d['count'] ?? 0).' model files found',
            'file_activity' => ($d['saves'] ?? 0).' saves, '.($d['bytes_delta'] ?? 0).' bytes in '.basename($d['dir'] ?? ''),
            'agent_started' => 'agent started (previous exit: '.($d['previous_exit'] ?? '?').')',
            'agent_stopping' => 'agent stopping',
            'command_result' => 'command '.($d['command_id'] ?? '').': '.($d['status'] ?? ''),
            default => $type,
        };
    }
}
