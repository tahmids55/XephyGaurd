<?php

declare(strict_types=1);

use App\Http\Controllers\Web\AdminController;
use App\Http\Controllers\Web\AlertController;
use App\Http\Controllers\Web\AuthController;
use App\Http\Controllers\Web\DashboardController;
use App\Http\Controllers\Web\EnrollmentController;
use App\Http\Controllers\Web\EvidenceController;
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
Route::get('/notice.html', [PublicController::class, 'notice'])->name('notice.html');
Route::get('/js/data.js', [PublicController::class, 'demoData'])->name('demo.data');
Route::get('/ca.crt', [PublicController::class, 'caCert'])->name('ca.cert');
Route::get('/install/linux.sh', [PublicController::class, 'linuxInstaller'])->name('install.linux');
Route::get('/proxy.pac', [PublicController::class, 'proxyPac'])->name('proxy.pac');
Route::get('/downloads', [PublicController::class, 'downloadsIndex'])->name('downloads.index');
Route::get('/downloads.html', [PublicController::class, 'downloadsIndex'])->name('downloads.html');
Route::get('/downloads/{file}', [PublicController::class, 'download'])->name('downloads');

Route::group([], function () {
    Route::get('/setup', [SetupController::class, 'show'])->name('setup.show');
    Route::post('/setup', [SetupController::class, 'store'])->name('setup.store')->middleware('throttle:10,1');

    Route::get('/login', [AuthController::class, 'show'])->name('login')->middleware('guest');
    Route::get('/login.html', [AuthController::class, 'show'])->name('login.html');
    Route::post('/login', [AuthController::class, 'login'])->middleware('guest');
    Route::post('/logout', [AuthController::class, 'logout'])->name('logout')->middleware('auth');

    Route::middleware('auth')->group(function () {
        // assistant: live view, enroll PCs, start/stop sessions
        Route::middleware('role:assistant,instructor')->group(function () {
            Route::get('/', [DashboardController::class, 'index'])->name('dashboard');
            Route::get('/index.html', [DashboardController::class, 'index'])->name('dashboard.html');
            Route::get('/pcs/{pc}', [DashboardController::class, 'pc'])->name('pcs.show');
            Route::get('/alerts', [AlertController::class, 'index'])->name('alerts.index');
            Route::get('/alerts.html', [AlertController::class, 'index'])->name('alerts.html');
            Route::post('/alerts/ack-batch', [AlertController::class, 'acknowledgeBatch'])->name('alerts.ack_batch');
            Route::post('/alerts/{alert}/ack', [AlertController::class, 'acknowledge'])->name('alerts.ack');

            Route::get('/sessions', [SessionController::class, 'index'])->name('sessions.index');
            Route::get('/sessions.html', [SessionController::class, 'index'])->name('sessions.html');
            Route::post('/sessions', [SessionController::class, 'start'])->name('sessions.start');
            Route::post('/sessions/{session}/stop', [SessionController::class, 'stop'])->name('sessions.stop');
            Route::post('/sessions/{session}/profile', [SessionController::class, 'switchProfile'])->name('sessions.switch');
            Route::post('/sessions/{session}/network-lock', [SessionController::class, 'networkLock'])->name('sessions.network_lock');
            Route::post('/sessions/message', [SessionController::class, 'message'])->name('sessions.message');
            Route::post('/pcs/{pc}/isolate', [SessionController::class, 'isolate'])->name('pcs.isolate');
            Route::post('/pcs/{pc}/kill', [SessionController::class, 'kill'])->name('pcs.kill');
            Route::post('/pcs/{pc}/rescan', [SessionController::class, 'rescan'])->name('pcs.rescan');

            Route::get('/enroll', [EnrollmentController::class, 'index'])->name('enroll.index');
            Route::get('/enroll.html', [EnrollmentController::class, 'index'])->name('enroll.html');
            Route::post('/enroll/{enrollment}/approve', [EnrollmentController::class, 'approve'])->name('enroll.approve');
            Route::post('/enroll/approve-all', [EnrollmentController::class, 'approveAll'])->name('enroll.approve_all');
            Route::post('/enroll/{enrollment}/reject', [EnrollmentController::class, 'reject'])->name('enroll.reject');
            Route::post('/enroll/tokens', [EnrollmentController::class, 'createToken'])->name('enroll.token');
            Route::post('/enroll/tokens/{token}/revoke', [EnrollmentController::class, 'revokeToken'])->name('enroll.token_revoke');
            Route::post('/pcs/{pc}/rename', [EnrollmentController::class, 'rename'])->name('pcs.rename');
            Route::post('/lab/layout', [DashboardController::class, 'saveLayout'])->name('lab.layout');
        });

        // instructor: sessions, overrides, timetable, reports, roster
        Route::middleware('role:instructor')->group(function () {
            // F46 screen evidence: instructor+, only inside an audited live view, every view audited (S-12)
            Route::post('/pcs/{pc}/evidence', [EvidenceController::class, 'store'])->name('pcs.evidence');
            Route::get('/evidence/{capture}', [EvidenceController::class, 'show'])->name('evidence.show');
            // F102 shared clipboard: staff-only, labelled with the real user name (S-8)
            Route::get('/share', [PublicController::class, 'shareIndex'])->name('share.index');
            Route::get('/share.html', [PublicController::class, 'shareIndex'])->name('share.html');
            Route::post('/share', [PublicController::class, 'shareSave'])->name('share.save');
            Route::get('/share/raw', [PublicController::class, 'shareRaw'])->name('share.raw');
            Route::post('/sessions/{session}/pcs/{pc}/override', [SessionController::class, 'override'])->name('sessions.override');
            Route::get('/profiles', [ProfileController::class, 'index'])->name('profiles.index');
            Route::get('/profiles.html', [ProfileController::class, 'index'])->name('profiles.html');
            Route::post('/timetable', [ProfileController::class, 'saveEntry'])->name('timetable.store');
            Route::post('/timetable/{entry}/toggle', [ProfileController::class, 'toggleEntry'])->name('timetable.toggle');
            Route::delete('/timetable/{entry}', [ProfileController::class, 'deleteEntry'])->name('timetable.delete');

            Route::get('/reports', [ReportController::class, 'index'])->name('reports.index');
            Route::get('/reports.html', [ReportController::class, 'index'])->name('reports.html');
            Route::get('/report-session-{num}.html', [ReportController::class, 'showByNumber'])->whereNumber('num')->name('reports.session.html');
            Route::get('/reports/{session}', [ReportController::class, 'show'])->name('reports.show');
            Route::get('/reports/{session}/students/{student}', [ReportController::class, 'student'])->name('reports.student');
            Route::post('/reports/{session}/rescore', [ReportController::class, 'rescore'])->name('reports.rescore');
            Route::get('/reports/{session}/pdf', [ReportController::class, 'pdf'])->name('reports.pdf');
            Route::get('/reports/{session}/csv', [ReportController::class, 'csv'])->name('reports.csv');
            Route::get('/research/export', [ReportController::class, 'research'])->name('research.export');

            Route::get('/roster', [RosterController::class, 'index'])->name('roster.index');
            Route::get('/roster.html', [RosterController::class, 'index'])->name('roster.html');
            Route::post('/roster/import', [RosterController::class, 'import'])->name('roster.import');
            Route::post('/roster/{student}/consent', [RosterController::class, 'consent'])->name('roster.consent');
            Route::post('/roster/{student}/exempt', [RosterController::class, 'exempt'])->name('roster.exempt');
        });

        // admin: setup, users, policy
        Route::middleware('role:admin')->group(function () {
            Route::post('/profiles/{profile?}', [ProfileController::class, 'saveProfile'])->name('profiles.save');
            Route::delete('/profiles/{profile}', [ProfileController::class, 'deleteProfile'])->name('profiles.delete');

            Route::get('/signatures', [SignatureController::class, 'index'])->name('signatures.index');
            Route::get('/signatures.html', [SignatureController::class, 'index'])->name('signatures.html');
            Route::post('/blocklist', [SignatureController::class, 'addEntry'])->name('blocklist.add');
            Route::post('/blocklist/import', [SignatureController::class, 'import'])->name('blocklist.import');
            Route::post('/blocklist/{entry}/toggle', [SignatureController::class, 'toggleEntry'])->name('blocklist.toggle');
            Route::delete('/blocklist/{entry}', [SignatureController::class, 'deleteEntry'])->name('blocklist.delete');
            Route::post('/rules', [SignatureController::class, 'saveRule'])->name('rules.save');
            Route::post('/rules/{rule}/toggle', [SignatureController::class, 'toggleRule'])->name('rules.toggle');
            Route::post('/signatures/publish', [SignatureController::class, 'publish'])->name('signatures.publish');
            Route::post('/signatures/{version}/activate', [SignatureController::class, 'activate'])->name('signatures.activate');

            Route::post('/enroll/rotate-key', [EnrollmentController::class, 'rotateKey'])->name('enroll.rotate');
            Route::post('/pcs/{pc}/retire', [EnrollmentController::class, 'retire'])->name('pcs.retire');
            Route::post('/roster/pseudonymize', [RosterController::class, 'pseudonymize'])->name('roster.pseudonymize');

            Route::get('/health', [AdminController::class, 'health'])->name('admin.health');
            Route::get('/health.html', [AdminController::class, 'health'])->name('admin.health.html');
            Route::get('/admin-health.html', [AdminController::class, 'health'])->name('admin.health.alias');
            Route::get('/settings', [AdminController::class, 'settings'])->name('admin.settings');
            Route::get('/settings.html', [AdminController::class, 'settings'])->name('admin.settings.html');
            Route::get('/admin-settings.html', [AdminController::class, 'settings'])->name('admin.settings.alias');
            Route::post('/settings', [AdminController::class, 'saveSettings'])->name('admin.settings.save');
            Route::get('/users', [AdminController::class, 'users'])->name('admin.users');
            Route::get('/users.html', [AdminController::class, 'users'])->name('admin.users.html');
            Route::get('/admin-users.html', [AdminController::class, 'users'])->name('admin.users.alias');
            Route::post('/users/{user?}', [AdminController::class, 'saveUser'])->name('admin.users.save');
            Route::get('/audit', [AdminController::class, 'audit'])->name('admin.audit');
            Route::get('/audit.html', [AdminController::class, 'audit'])->name('admin.audit.html');
            Route::get('/admin-audit.html', [AdminController::class, 'audit'])->name('admin.audit.alias');
        });
    });
});
