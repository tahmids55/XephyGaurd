<?php

declare(strict_types=1);

namespace App\Http\Middleware;

use App\Models\Agent;
use Closure;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Log;
use Illuminate\Support\Facades\RateLimiter;
use Symfony\Component\HttpFoundation\Response;

/**
 * Agent authentication = client certificate (verified by nginx against the lab CA and forwarded as
 * X-SSL-Client-Cert) + bearer token. The certificate fingerprint must match the one issued at
 * enrollment, so a copied token alone is useless and so is a certificate without the token.
 */
class AuthenticateAgent
{
    public function handle(Request $request, Closure $next): Response
    {
        $token = $request->bearerToken();
        if (! is_string($token) || strlen($token) < 32) {
            return response()->json(['error' => 'unauthenticated'], 401);
        }
        // Always required (no opt-out): the request must come through the nginx mTLS front.
        $verify = (string) $request->header('X-SSL-Client-Verify');
        $pem = urldecode((string) $request->header('X-SSL-Client-Cert'));
        $fp = $pem !== '' ? @openssl_x509_fingerprint($pem, 'sha256') : false;
        if ($verify !== 'SUCCESS' || ! is_string($fp)) {
            Log::warning("AuthAgent mTLS failed: verify={$verify}");

            return response()->json(['error' => 'client certificate missing or does not match this agent'], 403);
        }
        // The token is only valid together with the cert it was issued with (S-11).
        $agent = Agent::query()->where('token_hash', Agent::tokenHash($token, $fp))->where('cert_fingerprint', $fp)
            ->whereNull('revoked_at')->with('pc')->first() ?? $this->upgradeLegacy($token, $fp);
        if (! $agent) {
            if (Agent::query()->where('cert_fingerprint', $fp)->whereNull('revoked_at')->exists()) {
                Log::warning('AuthAgent token does not belong to the presented client certificate');

                return response()->json(['error' => 'client certificate missing or does not match this agent'], 403);
            }

            return response()->json(['error' => 'unknown or revoked agent'], 401);
        }
        if (! $agent->pc || $agent->pc->retired_at) {
            return response()->json(['error' => 'unknown or revoked agent'], 401);
        }
        $key = 'agent:'.$agent->id;
        if (RateLimiter::tooManyAttempts($key, (int) config('xephyguard.agent_rate_limit_per_minute'))) {
            return response()->json(['error' => 'rate limited'], 429, ['Retry-After' => RateLimiter::availableIn($key)]);
        }
        RateLimiter::hit($key, 60);
        $request->attributes->set('agent', $agent);

        return $next($request);
    }

    /**
     * Agents enrolled before S-11 stored sha256(token). Accept such a row only with its own cert and
     * rewrite the hash to the cert-bound form, so the old form stops working after the first call.
     */
    private function upgradeLegacy(string $token, string $fp): ?Agent
    {
        $agent = Agent::query()->where('token_hash', hash('sha256', $token))->whereNull('revoked_at')->with('pc')->first();
        if (! $agent || (string) $agent->cert_fingerprint === '' || ! hash_equals((string) $agent->cert_fingerprint, $fp)) {
            return null;
        }
        $agent->forceFill(['token_hash' => Agent::tokenHash($token, $fp)])->save();

        return $agent;
    }
}
