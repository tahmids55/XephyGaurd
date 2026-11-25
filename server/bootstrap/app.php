<?php

use App\Http\Middleware\AuthenticateAgent;
use App\Http\Middleware\EnsureSetupComplete;
use App\Http\Middleware\InternalToken;
use App\Http\Middleware\RequireRole;
use Illuminate\Contracts\Auth\Middleware\AuthenticatesRequests;
use Illuminate\Foundation\Application;
use Illuminate\Foundation\Configuration\Exceptions;
use Illuminate\Foundation\Configuration\Middleware;
use Illuminate\Http\Request;

return Application::configure(basePath: dirname(__DIR__))
    ->withRouting(
        web: __DIR__.'/../routes/web.php',
        api: __DIR__.'/../routes/api.php',
        commands: __DIR__.'/../routes/console.php',
        health: '/up',
    )
    ->withBroadcasting(__DIR__.'/../routes/channels.php', ['middleware' => ['web', 'auth']])
    ->withMiddleware(function (Middleware $middleware): void {
        $middleware->alias([
            'agent' => AuthenticateAgent::class,
            'internal' => InternalToken::class,
            'role' => RequireRole::class,
        ]);
        // Runs before route middleware (auth), so a fresh install always lands on the wizard.
        $middleware->appendToGroup('web', EnsureSetupComplete::class);
        $middleware->prependToPriorityList(AuthenticatesRequests::class,
            EnsureSetupComplete::class);
        // nginx (same Docker network) terminates TLS and forwards the client address.
        $middleware->trustProxies(at: ['127.0.0.1', '172.16.0.0/12', '10.0.0.0/8'], headers: Request::HEADER_X_FORWARDED_FOR | Request::HEADER_X_FORWARDED_PROTO);
        $middleware->redirectGuestsTo(fn () => route('login'));
    })
    ->withExceptions(function (Exceptions $exceptions): void {
        $exceptions->shouldRenderJsonWhen(
            fn (Request $request) => $request->is('api/*') || $request->expectsJson(),
        );
    })->create();
