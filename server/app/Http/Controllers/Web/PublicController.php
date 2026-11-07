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
                $items .= "<li style='margin:12px 0;'><a href='/downloads/{$f}' style='display:inline-block;padding:12px 20px;background:#2563eb;color:#fff;text-decoration:none;border-radius:6px;font-weight:600;font-size:15px;box-shadow:0 2px 4px rgba(37,99,235,0.2);'>⬇ Download {$f}</a> <span style='color:#64748b;margin-left:10px;font-weight:500;'>({$size} MB)</span></li>";
            }
        }

        $lab = Lab::current();
        $enrollKey = $lab->enrollKey() ?? 'XG-A77CFB-6554C8-489B53';
        $caFingerprint = CertificateAuthority::fromConfig()->fingerprint();
        $serverIp = $lab->ccc_ip ?: Setting::get('ccc_ip', request()->getHost());
        $serverUrl = 'http://' . $serverIp . ':8000';

        $silentCmd = "XephyGuard-Setup-0.1.0.exe /S /KEY={$enrollKey} /CCC={$serverUrl} /CAFP={$caFingerprint}";

        $html = <<<HTML
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>XephyGuard Downloads & Agent Setup</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f1f5f9; color: #1e293b; margin: 0; padding: 24px; }
        .card { max-width: 760px; margin: 20px auto; background: #ffffff; border-radius: 12px; padding: 32px; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.07), 0 4px 6px -4px rgba(0,0,0,0.05); }
        h1 { margin-top: 0; font-size: 24px; color: #0f172a; display: flex; align-items: center; gap: 8px; }
        h1 span { color: #4f46e5; }
        h2 { font-size: 17px; margin-top: 28px; margin-bottom: 12px; color: #334155; border-bottom: 2px solid #f1f5f9; padding-bottom: 6px; }
        p.subtitle { color: #64748b; font-size: 15px; margin-top: -6px; margin-bottom: 24px; }
        ul { list-style: none; padding: 0; margin: 0; }
        .field { margin-bottom: 14px; }
        .field label { display: block; font-size: 13px; font-weight: 600; color: #475569; margin-bottom: 4px; }
        .input-group { display: flex; gap: 8px; }
        input.copyable { flex: 1; padding: 9px 12px; background: #f8fafc; border: 1px solid #cbd5e1; border-radius: 6px; font-family: monospace; font-size: 13px; color: #0f172a; outline: none; }
        input.copyable:focus { border-color: #6366f1; background: #fff; }
        button.copy-btn { padding: 9px 14px; background: #e2e8f0; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; font-weight: 600; font-size: 13px; cursor: pointer; transition: all 0.15s; }
        button.copy-btn:hover { background: #cbd5e1; }
        .cmd-box { background: #0f172a; border-radius: 8px; padding: 16px; color: #38bdf8; font-family: monospace; font-size: 13px; overflow-x: auto; position: relative; margin-top: 8px; }
        .cmd-text { white-space: pre-wrap; word-break: break-all; }
        .copy-cmd-btn { margin-top: 10px; display: inline-block; padding: 8px 14px; background: #334155; color: #f8fafc; border: 1px solid #475569; border-radius: 5px; font-size: 12px; font-weight: 600; cursor: pointer; }
        .copy-cmd-btn:hover { background: #475569; }
        .footer-links { margin-top: 32px; padding-top: 16px; border-top: 1px solid #e2e8f0; font-size: 14px; color: #64748b; display: flex; gap: 16px; justify-content: space-between; align-items: center; }
        .footer-links a { color: #4f46e5; text-decoration: none; font-weight: 500; }
        .footer-links a:hover { text-decoration: underline; }
    </style>
</head>
<body>
<div class="card">
    <h1>Xephy<span>Guard</span> Agent Downloads & Setup</h1>
    <p class="subtitle">Download the Windows agent installer and configure student PCs with the connection credentials below.</p>

    <h2>1. Download Installer</h2>
    <ul>
        {$items}
    </ul>

    <h2>2. Connection Credentials</h2>
    <div class="field">
        <label>Teacher / CCC Server URL</label>
        <div class="input-group">
            <input class="copyable" readonly id="val-ccc" value="{$serverUrl}" onclick="this.select()">
            <button class="copy-btn" onclick="copyField('val-ccc')">Copy</button>
        </div>
    </div>
    <div class="field">
        <label>Lab Enrollment Key</label>
        <div class="input-group">
            <input class="copyable" readonly id="val-key" value="{$enrollKey}" onclick="this.select()">
            <button class="copy-btn" onclick="copyField('val-key')">Copy</button>
        </div>
    </div>
    <div class="field">
        <label>Server CA SHA-256 Fingerprint</label>
        <div class="input-group">
            <input class="copyable" readonly id="val-cafp" value="{$caFingerprint}" onclick="this.select()">
            <button class="copy-btn" onclick="copyField('val-cafp')">Copy</button>
        </div>
    </div>

    <h2>3. Fast Command Line Setup (Administrator CMD)</h2>
    <p style="font-size: 13px; color: #64748b; margin-top: 4px;">Run this command in cmd / PowerShell as Administrator from the folder containing the downloaded setup:</p>
    <div class="cmd-box">
        <div class="cmd-text" id="val-cmd">{$silentCmd}</div>
        <button class="copy-cmd-btn" onclick="copyCmd()">📋 Copy Full Command</button>
    </div>

    <div class="footer-links">
        <div>
            Root CA: <a href="/ca.crt">Download ca.crt</a> | Linux: <a href="/install/linux.sh">install/linux.sh</a>
        </div>
        <div>
            <a href="/login">Instructor Login →</a>
        </div>
    </div>
</div>

<script>
function copyField(id) {
    const el = document.getElementById(id);
    el.select();
    navigator.clipboard.writeText(el.value).then(() => {
        alert('Copied to clipboard: ' + el.value);
    });
}
function copyCmd() {
    const text = document.getElementById('val-cmd').innerText.trim();
    navigator.clipboard.writeText(text).then(() => {
        alert('Command copied to clipboard!');
    });
}
</script>
</body>
</html>
HTML;

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
