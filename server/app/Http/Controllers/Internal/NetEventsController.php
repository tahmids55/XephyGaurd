<?php

namespace App\Http\Controllers\Internal;

use App\Events\AlertRaised;
use App\Events\PcUpdated;
use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\Pc;
use App\Models\SessionPc;
use App\Services\EventIngestor;
use Carbon\CarbonImmutable;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

/** Receives blocked/allowed/observed network events from the proxy addon (and DNS log shipper). */
class NetEventsController extends Controller
{
    public function store(Request $request, EventIngestor $ingestor): JsonResponse
    {
        $data = $request->validate([
            'events' => 'required|array|max:1000',
            'events.*.client_ip' => 'required|ip',
            'events.*.host' => 'nullable|string|max:255',
            'events.*.path' => 'nullable|string|max:512',
            'events.*.layer' => 'required|string|max:16',
            'events.*.action' => 'required|in:blocked,allowed,rewritten,observed',
            'events.*.category' => 'nullable|string|max:24',
            'events.*.matched' => 'nullable|string|max:255',
            'events.*.ts' => 'nullable|integer',
        ]);
        $normIp = fn (string $ip): string => str_starts_with($ip, '::ffff:') ? substr($ip, 7) : $ip;
        $ips = collect($data['events'])->map(fn ($e) => $normIp($e['client_ip']))->unique();
        $pcs = Pc::query()->active()->whereIn('ip', $ips)->get()->keyBy('ip');
        $sessions = [];
        $rows = [];
        $dirtyPcs = [];
        $alerts = [];
        $seenAlertKeys = [];
        $pcsNeedingScore = [];
        foreach ($data['events'] as $e) {
            $clientIp = $normIp($e['client_ip']);
            $pc = $pcs[$clientIp] ?? null;
            $session = null;
            $sid = null;
            if ($pc) {
                $sessions[$pc->lab_id] ??= LabSession::activeFor($pc->lab_id);
                $session = $sessions[$pc->lab_id];
                $sid = $session?->id;
            }
            $at = isset($e['ts']) ? CarbonImmutable::createFromTimestampMs($e['ts'], config('app.timezone')) : now()->toImmutable();
            $rows[] = [
                'pc_id' => $pc?->id, 'lab_session_id' => $sid, 'client_ip' => $clientIp,
                'host' => $e['host'] ?? '', 'path' => $e['path'] ?? null, 'layer' => $e['layer'], 'action' => $e['action'],
                'category' => $e['category'] ?? null, 'matched' => $e['matched'] ?? null,
                'occurred_at' => $at,
            ];
            if (! $pc) {
                continue;
            }
            if ($e['action'] === 'blocked') {
                $pc->last_event = mb_substr('Blocked: '.($e['host'] ?? ''), 0, 160);
                $dirtyPcs[$pc->id] = $pc;
            } elseif ($e['action'] === 'observed' && in_array($e['category'] ?? '', ['ai', 'ide'], true)) {
                $prefix = ($e['category'] === 'ide') ? 'IDE AI traffic: ' : 'AI traffic: ';
                $hostOrMatched = ($e['host'] ?? '') ?: ($e['matched'] ?? '');
                $pc->last_event = mb_substr($prefix.$hostOrMatched, 0, 160);
                $dirtyPcs[$pc->id] = $pc;

                $ruleId = $e['category'] === 'ide' ? 'proxy.ide_ai_observed' : 'proxy.ai_observed';
                $label = $e['category'] === 'ide' ? 'IDE AI backend traffic' : 'AI service traffic';
                $subject = ($e['matched'] ?? '') ?: ($e['host'] ?? '');
                $title = mb_substr("{$label}: {$subject} (proxy)", 0, 200);
                $dedupKey = "{$pc->id}|{$sid}|{$ruleId}|{$title}";
                if (! isset($seenAlertKeys[$dedupKey])) {
                    $seenAlertKeys[$dedupKey] = true;
                    $dup = Alert::query()->where('pc_id', $pc->id)->where('lab_session_id', $sid)
                        ->where('rule_id', $ruleId)->where('title', $title)
                        ->where('occurred_at', '>', $at->subMinutes(30))
                        ->whereNull('acknowledged_at')  // high: a reviewed alert does not suppress a repeat
                        ->exists();
                    if (! $dup) {
                        $alert = Alert::create([
                            'pc_id' => $pc->id, 'lab_session_id' => $sid, 'rule_id' => $ruleId,
                            'severity' => 'high', 'category' => $e['category'],
                            'title' => $title,
                            'details' => [
                                'host' => $e['host'] ?? '',
                                'matched' => $e['matched'] ?? null,
                                'layer' => $e['layer'],
                                'source' => 'proxy',
                            ],
                            'user' => $pc->current_user,
                            'occurred_at' => $at,
                        ]);
                        $alert->setRelation('pc', $pc);
                        $alerts[] = $alert;
                        $pcsNeedingScore[$pc->id] = $session;
                    }
                }
            }
        }
        foreach (array_chunk($rows, 200) as $chunk) {
            NetEvent::query()->insert($chunk);
        }
        foreach ($dirtyPcs as $pcId => $pc) {
            if (array_key_exists($pcId, $pcsNeedingScore)) {
                $sess = $pcsNeedingScore[$pcId];
                $inSess = $sess && SessionPc::query()->where('lab_session_id', $sess->id)->where('pc_id', $pc->id)->exists();
                $ingestor->refreshLiveScore($pc, $inSess ? $sess : null);
            }
            $pc->save();
            broadcast(new PcUpdated($pc));
        }
        foreach ($alerts as $alert) {
            broadcast(new AlertRaised($alert));
        }

        return response()->json(['stored' => count($rows)]);
    }
}
