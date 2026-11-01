<?php

namespace App\Http\Middleware;

use App\Models\Agent;
use Closure;
use Illuminate\Http\Request;
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
        $agent = Agent::query()->where('token_hash', hash('sha256', $token))->whereNull('revoked_at')->with('pc')->first();
        if (! $agent || ! $agent->pc || $agent->pc->retired_at) {
            return response()->json(['error' => 'unknown or revoked agent'], 401);
        }
        if (config('xephyguard.require_mtls')) {
            $verify = $request->header('X-SSL-Client-Verify');
            $pem = urldecode((string) $request->header('X-SSL-Client-Cert'));
            $fp = $pem ? @openssl_x509_fingerprint($pem, 'sha256') : false;
            if ($verify !== 'SUCCESS' || ! $fp || ! hash_equals((string) $agent->cert_fingerprint, $fp)) {
                return response()->json(['error' => 'client certificate missing or does not match this agent'], 403);
            }
        }
        $key = 'agent:'.$agent->id;
        if (RateLimiter::tooManyAttempts($key, config('xephyguard.agent_rate_limit_per_minute'))) {
            return response()->json(['error' => 'rate limited'], 429, ['Retry-After' => RateLimiter::availableIn($key)]);
        }
        RateLimiter::hit($key, 60);
        $request->attributes->set('agent', $agent);

        return $next($request);
    }
}
