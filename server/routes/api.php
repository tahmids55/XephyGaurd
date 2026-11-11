<?php

use App\Http\Controllers\Agent\AgentController;
use App\Http\Controllers\Agent\EnrollController;
use App\Http\Controllers\Internal\EvalController;
use App\Http\Controllers\Internal\NetEventsController;
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
});

// Screen view gateway token issuance and lease renewal
Route::middleware(['web', 'auth'])->group(function () {
    Route::post('/screen/{student}/token', [\App\Http\Controllers\Api\ViewTokenController::class, 'issueToken']);
    Route::post('/screen/pc/{pc}/token', [\App\Http\Controllers\Api\ViewTokenController::class, 'issuePcToken']);
    Route::post('/screen/{audit}/renew', [\App\Http\Controllers\Api\ViewTokenController::class, 'renewToken']);
    Route::post('/screen/{audit}/stop', [\App\Http\Controllers\Api\ViewTokenController::class, 'stopView']);
});
