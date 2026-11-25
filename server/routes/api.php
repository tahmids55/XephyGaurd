<?php

use App\Http\Controllers\Agent\AgentController;
use App\Http\Controllers\Agent\EnrollController;
use App\Http\Controllers\Api\ViewTokenController;
use App\Http\Controllers\Internal\EvalController;
use App\Http\Controllers\Internal\NetEventsController;
use App\Http\Controllers\Internal\ScreenAgentController;
use Illuminate\Support\Facades\Route;

// Agent endpoint (served on :8443 behind nginx with TLS 1.3 client-certificate verification).
Route::prefix('agent/v1')->group(function () {
    Route::post('/enroll', [EnrollController::class, 'store'])->middleware('throttle:30,1');
    Route::get('/enroll/{enrollment}', [EnrollController::class, 'show'])->middleware('throttle:120,1');

    Route::middleware('agent')->group(function () {
        Route::get('/policy', [AgentController::class, 'policy']);
        Route::post('/events', [AgentController::class, 'events']);
        Route::post('/broadcasting/auth', [AgentController::class, 'broadcastAuth']);
    });
});

// CCC-internal services (proxy addon, analysis); never exposed to the lab LAN.
Route::prefix('internal')->middleware('internal')->group(function () {
    Route::post('/net-events', [NetEventsController::class, 'store']);
    Route::get('/eval/results', [EvalController::class, 'results']);
    Route::get('/screen/agent/{fingerprint}', [ScreenAgentController::class, 'show'])->where('fingerprint', '[0-9a-f]{64}');
});

// Screen view gateway token issuance and lease renewal.
// Authorisation rule: only active instructors and admins may watch a screen. During an active
// session only its owner (or an admin) may; a PC with no active session may be watched by any
// active instructor, and every view is written to screen_view_audits.
Route::middleware(['web', 'auth', 'role:instructor'])->group(function () {
    Route::post('/screen/{student}/token', [ViewTokenController::class, 'issueToken']);
    Route::post('/screen/pc/{pc}/token', [ViewTokenController::class, 'issuePcToken']);
    Route::post('/screen/{audit}/renew', [ViewTokenController::class, 'renewToken']);
    Route::post('/screen/{audit}/stop', [ViewTokenController::class, 'stopView']);
});
