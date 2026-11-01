<?php

namespace App\Http\Middleware;

use Closure;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

/** Usage: ->middleware('role:instructor,assistant'). Admins pass every role check. */
class RequireRole
{
    public function handle(Request $request, Closure $next, string ...$roles): Response
    {
        $user = $request->user();
        abort_unless($user && $user->active && $user->hasRole(...$roles), 403, 'Your role does not allow this action.');

        return $next($request);
    }
}
