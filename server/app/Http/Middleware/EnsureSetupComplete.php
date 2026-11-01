<?php

namespace App\Http\Middleware;

use App\Models\User;
use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

/** Sends every dashboard request to the first-run wizard until an admin exists. */
class EnsureSetupComplete
{
    public function handle(Request $request, Closure $next): Response
    {
        $exempt = $request->routeIs('setup.*', 'notice', 'ca.cert', 'install.linux', 'downloads') || $request->is('up');
        if (! $exempt && ! User::query()->where('role', 'admin')->exists()) {
            return redirect()->route('setup.show');
        }

        return $next($request);
    }
}
