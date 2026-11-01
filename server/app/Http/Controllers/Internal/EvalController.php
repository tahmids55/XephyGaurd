<?php

namespace App\Http\Controllers\Internal;

use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\NetEvent;
use App\Models\Pc;
use Carbon\CarbonImmutable;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

/** Evidence lookup for the red-team harness (eval/harness): what did the CCC see from a PC since T? */
class EvalController extends Controller
{
    public function results(Request $request): JsonResponse
    {
        $d = $request->validate(['pc_ip' => 'required|ip', 'since_ms' => 'required|integer']);
        $pc = Pc::query()->active()->where('ip', $d['pc_ip'])->first();
        $since = CarbonImmutable::createFromTimestampMs($d['since_ms']);

        return response()->json([
            'pc' => $pc?->only(['id', 'name', 'status', 'mode', 'last_seen_at', 'isolated']),
            'alerts' => $pc ? Alert::query()->where('pc_id', $pc->id)->where('created_at', '>=', $since)->orderBy('id')
                ->get(['rule_id', 'severity', 'category', 'title', 'occurred_at', 'created_at']) : [],
            'net_events' => NetEvent::query()->where('client_ip', $d['pc_ip'])->where('occurred_at', '>=', $since)->orderBy('id')
                ->limit(500)->get(['host', 'path', 'layer', 'action', 'category', 'occurred_at']),
            'server_time_ms' => now()->getTimestampMs(),
        ]);
    }
}
