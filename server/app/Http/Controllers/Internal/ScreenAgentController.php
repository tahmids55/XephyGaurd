<?php

namespace App\Http\Controllers\Internal;

use App\Http\Controllers\Controller;
use App\Models\Agent;
use Illuminate\Http\JsonResponse;

/**
 * Screen gateway lookup: which PC does this agent certificate belong to? The gateway binds an
 * agent connection to that PC's stream (screen Step 3). Unknown, revoked or retired → 404.
 */
class ScreenAgentController extends Controller
{
    public function show(string $fingerprint): JsonResponse
    {
        $agent = Agent::query()->where('cert_fingerprint', $fingerprint)->whereNull('revoked_at')->with('pc')->first();
        if (! $agent || ! $agent->pc || $agent->pc->retired_at) {
            return response()->json(['error' => 'unknown agent'], 404);
        }

        return response()->json(['pc_id' => $agent->pc->id]);
    }
}
