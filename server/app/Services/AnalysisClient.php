<?php

namespace App\Services;

use Illuminate\Support\Facades\Http;

/** Talks to the Python analysis service (analysis/), which reads the session data from MySQL,
 * computes v1 (and optionally v2) scores, writes the `scores` table and returns a summary. */
class AnalysisClient
{
    public function scoreSession(int $sessionId): array
    {
        return Http::timeout(120)->acceptJson()
            ->withToken((string) config('xephyguard.internal_token'))
            ->post(rtrim(config('xephyguard.analysis_url'), '/').'/score', ['session_id' => $sessionId])
            ->throw()->json();
    }

    public function health(): bool
    {
        try {
            return Http::timeout(3)->get(rtrim(config('xephyguard.analysis_url'), '/').'/health')->ok();
        } catch (\Throwable) {
            return false;
        }
    }
}
