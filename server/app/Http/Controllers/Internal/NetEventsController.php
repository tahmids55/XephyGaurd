<?php

namespace App\Http\Controllers\Internal;

use App\Http\Controllers\Controller;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\Pc;
use Carbon\CarbonImmutable;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

/** Receives blocked/allowed network events from the proxy addon (and DNS log shipper). */
class NetEventsController extends Controller
{
    public function store(Request $request): JsonResponse
    {
        $data = $request->validate([
            'events' => 'required|array|max:1000',
            'events.*.client_ip' => 'required|ip',
            'events.*.host' => 'nullable|string|max:255',
            'events.*.path' => 'nullable|string|max:512',
            'events.*.layer' => 'required|string|max:16',
            'events.*.action' => 'required|in:blocked,allowed,rewritten',
            'events.*.category' => 'nullable|string|max:24',
            'events.*.matched' => 'nullable|string|max:255',
            'events.*.ts' => 'nullable|integer',
        ]);
        $pcs = Pc::query()->active()->whereIn('ip', collect($data['events'])->pluck('client_ip')->unique())->get()->keyBy('ip');
        $sessions = [];
        $rows = [];
        foreach ($data['events'] as $e) {
            $pc = $pcs[$e['client_ip']] ?? null;
            $sid = null;
            if ($pc) {
                $sessions[$pc->lab_id] ??= LabSession::activeFor($pc->lab_id)?->id;
                $sid = $sessions[$pc->lab_id];
            }
            $rows[] = [
                'pc_id' => $pc?->id, 'lab_session_id' => $sid, 'client_ip' => $e['client_ip'],
                'host' => $e['host'] ?? '', 'path' => $e['path'] ?? null, 'layer' => $e['layer'], 'action' => $e['action'],
                'category' => $e['category'] ?? null, 'matched' => $e['matched'] ?? null,
                'occurred_at' => isset($e['ts']) ? CarbonImmutable::createFromTimestampMs($e['ts']) : now(),
            ];
            if ($pc && $e['action'] === 'blocked') {
                $pc->last_event = mb_substr('Blocked: '.($e['host'] ?? ''), 0, 160);
                $pc->save();
            }
        }
        foreach (array_chunk($rows, 200) as $chunk) {
            NetEvent::query()->insert($chunk);
        }

        return response()->json(['stored' => count($rows)]);
    }
}
