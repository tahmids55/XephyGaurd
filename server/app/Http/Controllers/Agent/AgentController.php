<?php

namespace App\Http\Controllers\Agent;

use App\Http\Controllers\Controller;
use App\Models\Agent;
use App\Services\EventIngestor;
use App\Services\PolicyBuilder;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

class AgentController extends Controller
{
    private function agent(Request $r): Agent
    {
        return $r->attributes->get('agent');
    }

    public function policy(Request $request, PolicyBuilder $policies): JsonResponse
    {
        return response()->json($policies->response($this->agent($request)->pc));
    }

    public function events(Request $request, EventIngestor $ingestor): JsonResponse
    {
        abort_if(strlen($request->getContent()) > config('xephyguard.max_batch_bytes'), 413);
        $batch = $request->json()->all();

        return response()->json($ingestor->ingest($this->agent($request), $batch, $request->ip()));
    }

    /** Signs the agent's own private channel subscription (Pusher protocol, used by Reverb). */
    public function broadcastAuth(Request $request): JsonResponse
    {
        $data = $request->validate(['socket_id' => ['required', 'regex:/^\d+\.\d+$/'], 'channel_name' => 'required|string']);
        $agent = $this->agent($request);

        if ($data['channel_name'] !== "private-pc.{$agent->pc_id}") {
            \Illuminate\Support\Facades\Log::warning("AgentController broadcastAuth 403: requested={$data['channel_name']}, expected=private-pc.{$agent->pc_id}");
            return response()->json(['error' => 'agents may only subscribe to their own channel'], 403);
        }

        $key = config('reverb.apps.apps.0.key');
        $secret = config('reverb.apps.apps.0.secret');
        $sig = hash_hmac('sha256', $data['socket_id'].':'.$data['channel_name'], $secret);

        return response()->json(['auth' => "$key:$sig"]);
    }
}
