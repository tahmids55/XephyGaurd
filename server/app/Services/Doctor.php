<?php

namespace App\Services;

use App\Models\Pc;
use App\Models\Setting;
use App\Models\SignatureVersion;
use Illuminate\Support\Facades\DB;

/**
 * Health checks with plain-language explanations (dashboard "System health" and
 * `php artisan xephyguard:doctor`). Host-level checks (IP forwarding, nftables) are in
 * ccc-deploy/bin/xephyguard-ccc doctor, which also calls this.
 */
class Doctor
{
    /** @return list<array{check: string, ok: bool, level: string, detail: string, fix: string}> */
    public function run(): array
    {
        $r = [];
        $add = function (string $check, bool $ok, string $detail, string $fix = '', string $level = 'error') use (&$r) {
            $r[] = ['check' => $check, 'ok' => $ok, 'level' => $ok ? 'ok' : $level, 'detail' => $detail, 'fix' => $ok ? '' : $fix];
        };

        try {
            DB::select('select 1');
            $add('Database', true, 'Connected to '.DB::connection()->getDriverName());
        } catch (\Throwable $e) {
            $add('Database', false, 'Cannot reach the database: '.$e->getMessage(), 'Check the mysql container: docker compose ps mysql');
        }

        $ca = CertificateAuthority::fromConfig();
        if (! $ca->exists()) {
            $add('Lab certificate authority', false, 'No CA found in '.$ca->dir(), 'Run: php artisan xephyguard:ca:init');
        } else {
            $days = (int) floor(($ca->expiresAt()->getTimestamp() - time()) / 86400);
            $add('Lab certificate authority', $days > 60, "CA valid for $days more days (fingerprint ".substr($ca->fingerprint(), 0, 16).'…)',
                'Plan a CA rotation and re-install the CA on lab PCs', 'warning');
        }

        $sig = SignatureVersion::active();
        $add('Detection signatures', (bool) $sig, $sig ? "Active version {$sig->version}" : 'No signature bundle published',
            'Open Signatures and press "Publish", or run php artisan xephyguard:signatures:publish');

        $state = config('xephyguard.net_state_path');
        $fresh = is_file($state);
        $add('Network policy file', $fresh, $fresh ? 'Written '.date('Y-m-d H:i:s', filemtime($state)) : "Missing: $state",
            'Run php artisan xephyguard:net:write and check the netpolicy container logs');

        $proxyOk = $this->tcp(Setting::get('ccc_ip'), (int) config('xephyguard.proxy_port'));
        $add('Proxy reachable', $proxyOk, $proxyOk ? 'Transparent proxy is listening' : 'Nothing listens on the proxy port',
            'docker compose logs proxy', 'warning');

        $dnsOk = $this->tcp(Setting::get('ccc_ip'), 53);
        $add('Lab DNS resolver', $dnsOk, $dnsOk ? 'Resolver answers on port 53' : 'Resolver not reachable on port 53',
            'docker compose logs dns; make sure systemd-resolved does not hold port 53 (install.sh disables its stub listener)', 'warning');

        $free = @disk_free_space(storage_path());
        $gb = $free ? round($free / 1e9, 1) : 0;
        $add('Disk space', $gb > 5, "$gb GB free", 'Free disk space or shorten raw-event retention in Settings', 'warning');

        $add('Analysis service', app(AnalysisClient::class)->health(), 'Behavioral scoring service',
            'docker compose logs analysis', 'warning');

        $queueBacklog = DB::table('jobs')->count();
        $add('Background jobs', $queueBacklog < 100, "$queueBacklog queued jobs", 'docker compose logs worker', 'warning');

        $silent = Pc::query()->active()->where('last_seen_at', '<', now()->subMinutes(10))->count();
        $add('Lab PCs reporting', $silent === 0, $silent ? "$silent PCs have not reported for 10+ minutes" : 'All enrolled PCs reporting',
            'Check those PCs are powered on and the agent service is running', 'warning');

        $outdated = Pc::query()->active()->with('agent')->get()->filter(fn ($p) => $p->agent && $p->agent->cert_expires_at?->lt(now()->addDays(30)))->count();
        $add('Agent certificates', $outdated === 0, $outdated ? "$outdated agent certificates expire within 30 days" : 'All agent certificates valid',
            'Re-enroll the affected PCs', 'warning');

        $clockSkew = Pc::query()->active()->with('agent')->get()->filter(fn ($p) => abs($p->agent?->clock_offset_ms ?? 0) > 60000)->count();
        $add('Time sync', $clockSkew === 0, $clockSkew ? "$clockSkew PCs have a clock more than 1 minute off" : 'PC clocks agree with the CCC',
            'Enable NTP on the lab PCs (the CCC still corrects event times)', 'warning');

        return $r;
    }

    private function tcp(?string $host, int $port): bool
    {
        if (! $host) {
            return false;
        }
        $s = @fsockopen($host, $port, $errno, $errstr, 1.0);
        if ($s) {
            fclose($s);

            return true;
        }

        return false;
    }
}
