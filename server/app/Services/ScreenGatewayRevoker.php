<?php

namespace App\Services;

use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Log;

/**
 * Ends open screen views on the gateway (loopback POST /internal/revoke, docs/PROTOCOL.md).
 * Best effort: a gateway that is down must never block stopping a view, logging out or ending a
 * session; the lease then still ends at its expiry (at most the token TTL, 30 s).
 */
class ScreenGatewayRevoker
{
    public function audit(int $auditId): void
    {
        $this->send(['audit_id' => $auditId]);
    }

    public function user(int $userId): void
    {
        $this->send(['user_id' => $userId]);
    }

    public function session(int $labSessionId): void
    {
        $this->send(['lab_session_id' => $labSessionId]);
    }

    private function send(array $selector): void
    {
        $token = (string) config('xephyguard.internal_token');
        if ($token === '') {
            return;
        }
        $url = rtrim((string) config('services.screen_gateway.internal_url'), '/').'/internal/revoke';
        try {
            $res = Http::timeout(2)->withToken($token)->acceptJson()->post($url, $selector);
            if (! $res->ok()) {
                Log::warning('screen gateway revoke failed', ['status' => $res->status()] + $selector);
            }
        } catch (\Throwable $e) {
            Log::warning('screen gateway revoke unreachable', ['error' => $e->getMessage()] + $selector);
        }
    }
}
