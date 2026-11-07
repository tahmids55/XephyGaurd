<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\Event;
use App\Models\Pc;
use App\Models\Setting;
use App\Models\SignatureVersion;
use App\Services\CertificateAuthority;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Http\Response;
use Illuminate\Support\Facades\DB;

class HealthController extends Controller
{
    public function healthz(): JsonResponse
    {
        $dbOk = false;
        $queueDepth = 0;
        try {
            DB::select('select 1');
            $dbOk = true;
            $queueDepth = (int) DB::table('jobs')->count();
        } catch (\Throwable) {
            $dbOk = false;
        }

        $ca = CertificateAuthority::fromConfig();
        $caInitialized = $ca->exists();
        $activeSig = $dbOk ? SignatureVersion::active()?->version : null;

        $pcs = $dbOk ? Pc::query()->active()->get(['id', 'isolated', 'last_seen_at']) : collect();
        $pcsOnline = $pcs->filter(fn (Pc $p) => $p->online())->count();
        $pcsIsolated = $pcs->where('isolated', true)->count();

        $status = ($dbOk && $caInitialized) ? 'ok' : 'degraded';

        return response()->json([
            'status' => $status,
            'db' => $dbOk,
            'ca_initialized' => $caInitialized,
            'active_signature_version' => $activeSig,
            'pcs_online' => $pcsOnline,
            'pcs_isolated' => $pcsIsolated,
            'queue_depth' => $queueDepth,
            'timestamp' => now()->toIso8601String(),
        ], $status === 'ok' ? 200 : 503);
    }

    public function metrics(Request $request): Response
    {
        if (! $this->authorizedForMetrics($request)) {
            return response('Forbidden', 403, ['Content-Type' => 'text/plain; charset=utf-8']);
        }

        $pcs = Pc::query()->active()->with('agent')->get();
        $totalPcs = $pcs->count();
        $onlinePcs = $pcs->filter(fn (Pc $p) => $p->online())->count();
        $isolatedPcs = $pcs->where('isolated', true)->count();
        $silentPcs = $pcs->filter(fn (Pc $p) => ! $p->online() && ! $p->clean_shutdown && $p->last_seen_at !== null)->count();
        $eventsTotal = Event::query()->count();

        $openBySev = Alert::query()
            ->whereNull('acknowledged_at')
            ->selectRaw('severity, count(*) as n')
            ->groupBy('severity')
            ->pluck('n', 'severity')
            ->all();

        $byVersion = $pcs->groupBy(fn (Pc $p) => $p->agent?->version ?: 'unknown')->map->count();

        $lines = [
            '# HELP xephyguard_pcs_total Total active enrolled lab PCs.',
            '# TYPE xephyguard_pcs_total gauge',
            "xephyguard_pcs_total {$totalPcs}",
            '# HELP xephyguard_pcs_online Currently online lab PCs.',
            '# TYPE xephyguard_pcs_online gauge',
            "xephyguard_pcs_online {$onlinePcs}",
            '# HELP xephyguard_pcs_isolated Currently isolated lab PCs.',
            '# TYPE xephyguard_pcs_isolated gauge',
            "xephyguard_pcs_isolated {$isolatedPcs}",
            '# HELP xephyguard_pcs_silent Lab PCs that went silent without clean shutdown.',
            '# TYPE xephyguard_pcs_silent gauge',
            "xephyguard_pcs_silent {$silentPcs}",
            '# HELP xephyguard_events_total Total ingested agent telemetry events.',
            '# TYPE xephyguard_events_total counter',
            "xephyguard_events_total {$eventsTotal}",
            '# HELP xephyguard_alerts_open_total Unacknowledged alerts by severity.',
            '# TYPE xephyguard_alerts_open_total gauge',
        ];

        foreach (['info', 'low', 'medium', 'high', 'critical'] as $sev) {
            $count = (int) ($openBySev[$sev] ?? 0);
            $lines[] = "xephyguard_alerts_open_total{severity=\"{$sev}\"} {$count}";
        }

        $lines[] = '# HELP xephyguard_agents_by_version Enrolled agents grouped by version.';
        $lines[] = '# TYPE xephyguard_agents_by_version gauge';
        if ($byVersion->isEmpty()) {
            $lines[] = 'xephyguard_agents_by_version{version="none"} 0';
        } else {
            foreach ($byVersion as $ver => $count) {
                $safeVer = addslashes((string) $ver);
                $lines[] = "xephyguard_agents_by_version{version=\"{$safeVer}\"} {$count}";
            }
        }

        return response(implode("\n", $lines)."\n", 200, [
            'Content-Type' => 'text/plain; version=0.0.4; charset=utf-8',
        ]);
    }

    private function authorizedForMetrics(Request $request): bool
    {
        $user = $request->user();
        if ($user !== null && ($user->active ?? true)) {
            return true;
        }

        $configuredToken = config('xephyguard.metrics_token') ?: Setting::get('metrics_token');
        $bearer = $request->bearerToken();
        if (is_string($configuredToken) && $configuredToken !== '') {
            return is_string($bearer) && $bearer !== '' && hash_equals($configuredToken, $bearer);
        }

        if ($bearer !== null && $bearer !== '') {
            return false;
        }

        $ip = $request->server('REMOTE_ADDR') ?? $request->ip();

        return in_array($ip, ['127.0.0.1', '::1'], true) && ! $request->headers->has('X-Forwarded-For');
    }
}
