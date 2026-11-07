<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Lab;
use App\Models\Setting;
use App\Services\CertificateAuthority;
use Illuminate\Http\Response;

/** Unauthenticated pages: the student notice, the CA certificate and the agent install scripts. */
class PublicController extends Controller
{
    public function notice()
    {
        return view('public.notice', ['text' => Setting::get('notice_text'), 'lab' => Lab::query()->first()]);
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
        $script = view('public.install-linux', [
            'ccc' => Setting::get('ccc_ip'),
            'fingerprint' => CertificateAuthority::fromConfig()->fingerprint(),
        ])->render();

        return response($script, 200, ['Content-Type' => 'text/x-shellscript']);
    }

    public function downloadsIndex(): Response
    {
        $dir = storage_path('app/public/downloads');
        $files = is_dir($dir) ? array_values(array_diff(scandir($dir), ['.', '..'])) : [];
        $items = '';
        foreach ($files as $f) {
            if (preg_match('/^[A-Za-z0-9._-]+\.(deb|rpm|msi|exe|sha256)$/', $f)) {
                $size = round(filesize($dir.'/'.$f) / 1048576, 2);
                $items .= "<li style='margin:12px 0;'><a href='/downloads/{$f}' style='display:inline-block;padding:10px 18px;background:#2563eb;color:#fff;text-decoration:none;border-radius:6px;font-weight:600;'>Download {$f}</a> <span style='color:#64748b;margin-left:8px;'>({$size} MB)</span></li>";
            }
        }
        $html = "<!doctype html><html><head><meta charset='utf-8'><title>XephyGuard Downloads</title></head>"
              . "<body style='font-family:system-ui,sans-serif;max-width:650px;margin:40px auto;padding:20px;line-height:1.5;'>"
              . "<h2>XephyGuard Student PC Installers</h2>"
              . "<p>Click below to download the Windows Agent Installer:</p>"
              . "<ul style='list-style:none;padding:0;'>{$items}</ul>"
              . "<hr style='margin:24px 0;border:0;border-top:1px solid #e2e8f0;'>"
              . "<p><a href='/ca.crt' style='color:#2563eb;'>Download Lab Root CA Certificate (ca.crt)</a></p>"
              . "</body></html>";

        return response($html, 200, ['Content-Type' => 'text/html; charset=utf-8']);
    }

    public function download(string $file)
    {
        abort_unless(preg_match('/^[A-Za-z0-9._-]+\.(deb|rpm|msi|exe|sha256)$/', $file), 404);
        $path = storage_path('app/public/downloads/'.$file);
        abort_unless(is_file($path), 404, 'Installer not uploaded yet. Copy release artifacts to storage/app/public/downloads.');

        return response()->download($path);
    }
}
