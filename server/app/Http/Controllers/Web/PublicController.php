<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\Setting;
use App\Services\CertificateAuthority;
use App\Services\NoticeText;
use App\Services\PolicyService;
use Illuminate\Http\Request;
use Illuminate\Http\Response;

/**
 * Unauthenticated pages: the student notice, the CA certificate and the agent install scripts.
 * The share* actions (F102) are routed behind auth + role:instructor, not public.
 */
class PublicController extends Controller
{
    public function notice()
    {
        return view('public.notice', ['text' => NoticeText::text(), 'features' => NoticeText::features(), 'lab' => Lab::query()->first()]);
    }

    /** F26 demo dataset: served only when APP_ENV=local, 404 everywhere else (S-50). */
    public function demoData()
    {
        abort_unless(app()->environment('local'), 404);

        return response()->file(resource_path('demo/data.js'), ['Content-Type' => 'application/javascript']);
    }

    public function caCert(): Response
    {
        return response(CertificateAuthority::fromConfig()->certPem(), 200, [
            'Content-Type' => 'application/x-pem-file',
            'Content-Disposition' => 'attachment; filename="xephyguard-ca.crt"',
        ]);
    }

    /** `curl -fsSL http://<ccc>/install/linux.sh | sudo bash -s -- --key <KEY>` */
    public function linuxInstaller(): Response
    {
        $ccc = $this->resolveCccHost();

        $script = view('public.install-linux', [
            'ccc' => $ccc,
            'fingerprint' => CertificateAuthority::fromConfig()->fingerprint(),
        ])->render();

        return response($script, 200, ['Content-Type' => 'text/x-shellscript']);
    }

    public static function resolveWindowsInstallerName(): ?string
    {
        $dir = storage_path('app/public/downloads');
        if (! is_dir($dir)) {
            return null;
        }
        $matches = glob($dir.'/XephyGuard-Setup*.exe') ?: [];
        if ($matches === []) {
            return null;
        }
        usort($matches, fn (string $a, string $b): int => version_compare(basename($b), basename($a)));

        return basename($matches[0]);
    }

    public function downloadsIndex(): Response
    {
        $lab = Lab::current();
        $ca = CertificateAuthority::fromConfig();
        $ccc = $this->resolveCccHost($lab?->ccc_ip);
        $setupFile = self::resolveWindowsInstallerName();
        $files = collect(glob(storage_path('app/public/downloads/*')) ?: [])
            ->filter(fn ($p) => is_file($p))
            ->map(function ($p) {
                $bytes = (int) filesize($p);
                $sizeLabel = $bytes >= 1048576
                    ? round($bytes / 1048576, 1).' MB'
                    : max(1, (int) ceil($bytes / 1024)).' KB';

                return [
                    'name' => basename($p),
                    'size_mb' => round($bytes / 1048576, 1),
                    'size_label' => $sizeLabel,
                ];
            })->values()->all();

        return response()->view('public.downloads', [
            'ccc' => $ccc,
            'fingerprint' => $ca->exists() ? $ca->fingerprint() : null,
            'setupFile' => $setupFile,
            'files' => $files,
        ]);
    }

    public function download(string $file)
    {
        abort_unless(preg_match('/^[A-Za-z0-9._-]+\.(deb|rpm|msi|exe|bat|sha256)$/', $file), 404);
        $path = storage_path('app/public/downloads/'.$file);
        if (! is_file($path) && preg_match('/^xephyguard-agent_latest_([A-Za-z0-9]+)\.(deb|rpm)$/', $file, $m)) {
            $matches = glob(storage_path('app/public/downloads/xephyguard-agent_*_'.$m[1].'.'.$m[2])) ?: [];
            if ($matches !== []) {
                usort($matches, fn (string $a, string $b): int => version_compare(basename($b), basename($a)));
                $path = $matches[0];
            }
        }
        abort_unless(is_file($path), 404, 'Installer not uploaded yet. Copy release artifacts to storage/app/public/downloads.');

        return response()->download($path);
    }

    private function resolveCccHost(?string $fallback = null): string
    {
        $reqHost = request()->getHost();
        if (! in_array($reqHost, ['127.0.0.1', '::1', 'localhost', ''], true)) {
            return $reqHost;
        }

        return (string) ($fallback ?: Setting::get('ccc_ip', $reqHost) ?: $reqHost);
    }

    /** Dynamic Proxy Auto-Config (PAC) script for proxy_only lab PCs. */
    public function proxyPac(Request $request): Response
    {
        $mode = (string) Setting::get('network_mode', config('xephyguard.network_mode', 'gateway'));
        $host = (string) (Setting::get('ccc_ip') ?: $request->getHost());
        $port = (int) config('xephyguard.proxy_port', 8080);

        if ($mode !== 'proxy_only' || $host === '' || $host === 'localhost') {
            $js = "function FindProxyForURL(url, host) {\n  return \"DIRECT\";\n}\n";
        } else {
            $safeHost = addcslashes($host, '\\"');
            // A PC in a Test session (or isolated) gets no DIRECT fallback: if the proxy is unreachable its
            // browser fails closed instead of going straight to the Internet (plan alternative F97).
            $pc = Pc::query()->whereNull('retired_at')->where('ip', $request->ip())->first();
            $strict = $pc && ($pc->isolated || app(PolicyService::class)->effective($pc)['profile']->mode === 'test');
            $fallback = $strict ? '' : '; DIRECT';
            $js = <<<JS
function FindProxyForURL(url, host) {
  if (isPlainHostName(host) ||
      shExpMatch(host, "*.local") ||
      isInNet(dnsResolve(host), "10.0.0.0", "255.0.0.0") ||
      isInNet(dnsResolve(host), "172.16.0.0", "255.240.0.0") ||
      isInNet(dnsResolve(host), "192.168.0.0", "255.255.0.0") ||
      isInNet(dnsResolve(host), "127.0.0.0", "255.0.0.0") ||
      host === "{$safeHost}") {
    return "DIRECT";
  }
  return "PROXY {$safeHost}:{$port}{$fallback}";
}

JS;
        }

        return response($js, 200, [
            'Content-Type' => 'application/x-ns-proxy-autoconfig; charset=utf-8',
            'Cache-Control' => 'no-store, max-age=0',
        ]);
    }

    public function shareIndex(): Response
    {
        return response()->view('public.share', [
            'text' => (string) Setting::get('shared_clipboard', ''),
            'updatedAt' => (string) Setting::get('shared_clipboard_at', 'Never'),
            'history' => (array) Setting::get('shared_clipboard_history', []),
        ]);
    }

    public function shareSave()
    {
        $text = (string) request()->input('text', '');
        $cmd = (bool) request()->boolean('cmd');
        $at = now()->format('H:i');
        // The route requires auth + role:instructor, so there is always a real, named user.
        $by = (string) request()->user()->name;
        $last = "Today {$at} by {$by}";
        Setting::put('shared_clipboard', mb_substr($text, 0, 65000));
        Setting::put('shared_clipboard_at', $last);
        if (trim($text) !== '') {
            $hist = (array) Setting::get('shared_clipboard_history', []);
            array_unshift($hist, ['t' => $at, 'v' => mb_substr($text, 0, 2000), 'cmd' => $cmd]);
            Setting::put('shared_clipboard_history', array_slice($hist, 0, 20));
        }

        return response()->json(['ok' => true, 'updated_at' => $last]);
    }

    public function shareRaw()
    {
        return response()->json([
            'text' => (string) Setting::get('shared_clipboard', ''),
            'updated_at' => (string) Setting::get('shared_clipboard_at', 'Never'),
            'history' => (array) Setting::get('shared_clipboard_history', []),
        ]);
    }
}
