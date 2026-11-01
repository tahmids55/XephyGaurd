<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

/** For CCC-internal services (proxy event shipping, analysis callbacks). */
class InternalToken
{
    public function handle(Request $request, Closure $next): Response
    {
        $expected = (string) config('xephyguard.internal_token');
        abort_if($expected === '' || ! hash_equals($expected, (string) $request->bearerToken()), 401);

        return $next($request);
    }
}
