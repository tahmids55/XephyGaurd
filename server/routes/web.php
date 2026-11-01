<?php

use App\Http\Controllers\Web\AdminController;
use App\Http\Controllers\Web\AlertController;
use App\Http\Controllers\Web\AuthController;
use App\Http\Controllers\Web\DashboardController;
use App\Http\Controllers\Web\EnrollmentController;
use App\Http\Controllers\Web\ProfileController;
use App\Http\Controllers\Web\PublicController;
use App\Http\Controllers\Web\ReportController;
use App\Http\Controllers\Web\RosterController;
use App\Http\Controllers\Web\SessionController;
use App\Http\Controllers\Web\SetupController;
use App\Http\Controllers\Web\SignatureController;
use Illuminate\Support\Facades\Route;

// ---- public (no login): student notice, CA certificate, agent installers -------------------
Route::get('/notice', [PublicController::class, 'notice'])->name('notice');
Route::get('/ca.crt', [PublicController::class, 'caCert'])->name('ca.cert');
Route::get('/install/linux.sh', [PublicController::class, 'linuxInstaller'])->name('install.linux');
Route::get('/downloads/{file}', [PublicController::class, 'download'])->name('downloads');

Route::group([], function () {
    Route::get('/setup', [SetupController::class, 'show'])->name('setup.show');
    Route::post('/setup', [SetupController::class, 'store'])->name('setup.store')->middleware('throttle:10,1');

    Route::get('/login', [AuthController::class, 'show'])->name('login')->middleware('guest');
    Route::post('/login', [AuthController::class, 'login'])->middleware('guest');
    Route::post('/logout', [AuthController::class, 'logout'])->name('logout')->middleware('auth');

    Route::middleware('auth')->group(function () {
        // assistant: live view, enroll PCs, start/stop sessions
        Route::middleware('role:assistant,instructor')->group(function () {
            Route::get('/', [DashboardController::class, 'index'])->name('dashboard');
            Route::get('/pcs/{pc}', [DashboardController::class, 'pc'])->name('pcs.show');
            Route::get('/alerts', [AlertController::class, 'index'])->name('alerts.index');
            Route::post('/alerts/{alert}/ack', [AlertController::class, 'acknowledge'])->name('alerts.ack');

            Route::get('/sessions', [SessionController::class, 'index'])->name('sessions.index');
            Route::post('/sessions', [SessionController::class, 'start'])->name('sessions.start');
            Route::post('/sessions/{session}/stop', [SessionController::class, 'stop'])->name('sessions.stop');
            Route::post('/sessions/{session}/profile', [SessionController::class, 'switchProfile'])->name('sessions.switch');
            Route::post('/sessions/message', [SessionController::class, 'message'])->name('sessions.message');
            Route::post('/pcs/{pc}/isolate', [SessionController::class, 'isolate'])->name('pcs.isolate');
            Route::post('/pcs/{pc}/kill', [SessionController::class, 'kill'])->name('pcs.kill');

            Route::get('/enroll', [EnrollmentController::class, 'index'])->name('enroll.index');
            Route::post('/enroll/{enrollment}/approve', [EnrollmentController::class, 'approve'])->name('enroll.approve');
            Route::post('/enroll/approve-all', [EnrollmentController::class, 'approveAll'])->name('enroll.approve_all');
            Route::post('/enroll/{enrollment}/reject', [EnrollmentController::class, 'reject'])->name('enroll.reject');
            Route::post('/pcs/{pc}/rename', [EnrollmentController::class, 'rename'])->name('pcs.rename');
            Route::post('/lab/layout', [DashboardController::class, 'saveLayout'])->name('lab.layout');
        });

        // instructor: sessions, overrides, timetable, reports, roster
        Route::middleware('role:instructor')->group(function () {
            Route::post('/sessions/{session}/pcs/{pc}/override', [SessionController::class, 'override'])->name('sessions.override');
            Route::get('/profiles', [ProfileController::class, 'index'])->name('profiles.index');
            Route::post('/timetable', [ProfileController::class, 'saveEntry'])->name('timetable.store');
            Route::post('/timetable/{entry}/toggle', [ProfileController::class, 'toggleEntry'])->name('timetable.toggle');
            Route::delete('/timetable/{entry}', [ProfileController::class, 'deleteEntry'])->name('timetable.delete');

            Route::get('/reports', [ReportController::class, 'index'])->name('reports.index');
            Route::get('/reports/{session}', [ReportController::class, 'show'])->name('reports.show');
            Route::get('/reports/{session}/students/{student}', [ReportController::class, 'student'])->name('reports.student');
            Route::post('/reports/{session}/rescore', [ReportController::class, 'rescore'])->name('reports.rescore');
            Route::get('/reports/{session}/pdf', [ReportController::class, 'pdf'])->name('reports.pdf');
            Route::get('/reports/{session}/csv', [ReportController::class, 'csv'])->name('reports.csv');
            Route::get('/research/export', [ReportController::class, 'research'])->name('research.export');

            Route::get('/roster', [RosterController::class, 'index'])->name('roster.index');
            Route::post('/roster/import', [RosterController::class, 'import'])->name('roster.import');
            Route::post('/roster/{student}/consent', [RosterController::class, 'consent'])->name('roster.consent');
            Route::post('/roster/{student}/exempt', [RosterController::class, 'exempt'])->name('roster.exempt');
        });

        // admin: setup, users, policy
        Route::middleware('role:admin')->group(function () {
            Route::post('/profiles/{profile?}', [ProfileController::class, 'saveProfile'])->name('profiles.save');
            Route::delete('/profiles/{profile}', [ProfileController::class, 'deleteProfile'])->name('profiles.delete');

            Route::get('/signatures', [SignatureController::class, 'index'])->name('signatures.index');
            Route::post('/blocklist', [SignatureController::class, 'addEntry'])->name('blocklist.add');
            Route::post('/blocklist/import', [SignatureController::class, 'import'])->name('blocklist.import');
            Route::post('/blocklist/{entry}/toggle', [SignatureController::class, 'toggleEntry'])->name('blocklist.toggle');
            Route::delete('/blocklist/{entry}', [SignatureController::class, 'deleteEntry'])->name('blocklist.delete');
            Route::post('/rules', [SignatureController::class, 'saveRule'])->name('rules.save');
            Route::post('/rules/{rule}/toggle', [SignatureController::class, 'toggleRule'])->name('rules.toggle');
            Route::post('/signatures/publish', [SignatureController::class, 'publish'])->name('signatures.publish');
            Route::post('/signatures/{version}/activate', [SignatureController::class, 'activate'])->name('signatures.activate');

            Route::post('/enroll/rotate-key', [EnrollmentController::class, 'rotateKey'])->name('enroll.rotate');
            Route::post('/enroll/auto-approve', [EnrollmentController::class, 'autoApprove'])->name('enroll.auto');
            Route::post('/pcs/{pc}/retire', [EnrollmentController::class, 'retire'])->name('pcs.retire');
            Route::post('/roster/pseudonymize', [RosterController::class, 'pseudonymize'])->name('roster.pseudonymize');

            Route::get('/health', [AdminController::class, 'health'])->name('admin.health');
            Route::get('/settings', [AdminController::class, 'settings'])->name('admin.settings');
            Route::post('/settings', [AdminController::class, 'saveSettings'])->name('admin.settings.save');
            Route::get('/users', [AdminController::class, 'users'])->name('admin.users');
            Route::post('/users/{user?}', [AdminController::class, 'saveUser'])->name('admin.users.save');
            Route::get('/audit', [AdminController::class, 'audit'])->name('admin.audit');
        });
    });
});
