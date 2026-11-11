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
        $reqHost = request()->getHost();
        $ccc = (filter_var($reqHost, FILTER_VALIDATE_IP) && $reqHost !== '127.0.0.1')
            ? $reqHost
            : Setting::get('ccc_ip', $reqHost);

        $script = view('public.install-linux', [
            'ccc' => $ccc,
            'fingerprint' => CertificateAuthority::fromConfig()->fingerprint(),
        ])->render();

        return response($script, 200, ['Content-Type' => 'text/x-shellscript']);
    }

    public function downloadsIndex(): Response
    {
        $dir = storage_path('app/public/downloads');
        $files = is_dir($dir) ? array_values(array_diff(scandir($dir), ['.', '..'])) : [];
        $meta = [
            'XephyGuard-Setup-0.1.0.exe' => [
                'label' => 'Full Agent Installer (All-In-One)',
                'desc' => 'Complete Windows agent with built-in real-time screen streaming, AI detection, process tracking, USB events, and network policies. 100% automated background operation.',
                'badge' => 'All-In-One Setup',
            ],
            'xgagent.exe' => [
                'label' => 'Screen View Agent (Live Stream)',
                'desc' => 'Portable DXGI/GDI screen capture executable for instructor live screen monitoring and snapshots.',
                'badge' => 'Screen Monitor',
            ],
            'start-screen-stream.bat' => [
                'label' => '1-Click Screen Stream Launcher (Windows Batch)',
                'desc' => 'Double-click script to run xgagent.exe connected to this server with auto-prompted or default settings.',
                'badge' => 'Helper Script',
            ],
        ];

        $items = '';
        foreach ($files as $f) {
            if (preg_match('/^[A-Za-z0-9._-]+\.(deb|rpm|msi|exe|bat|sha256)$/', $f)) {
                $size = round(filesize($dir.'/'.$f) / 1048576, 2);
                $info = $meta[$f] ?? ['label' => $f, 'desc' => '', 'badge' => 'Installer'];
                $label = htmlspecialchars($info['label']);
                $desc = htmlspecialchars($info['desc']);
                $badge = htmlspecialchars($info['badge']);

                $items .= "<li style='margin:18px 0;padding:16px;background:#f8fafc;border:1px solid #e2e8f0;border-radius:8px;'>";
                $items .= "<div style='display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;'>";
                $items .= "<div><strong style='font-size:16px;color:#0f172a;'>{$label}</strong> <span style='display:inline-block;margin-left:8px;padding:2px 8px;background:#e0e7ff;color:#4338ca;border-radius:4px;font-size:11px;font-weight:600;'>{$badge}</span><div style='font-size:13px;color:#64748b;margin-top:4px;'>{$desc}</div></div>";
                $items .= "<div><a href='/downloads/{$f}' style='display:inline-block;padding:10px 18px;background:#2563eb;color:#fff;text-decoration:none;border-radius:6px;font-weight:600;font-size:14px;box-shadow:0 2px 4px rgba(37,99,235,0.2);'>⬇ Download {$f}</a> <span style='color:#64748b;margin-left:8px;font-size:13px;'>({$size} MB)</span></div>";
                $items .= "</div></li>";
            }
        }

        $lab = Lab::current();
        $enrollKey = $lab->enrollKey() ?? 'XG-A77CFB-6554C8-489B53';
        $caFingerprint = CertificateAuthority::fromConfig()->fingerprint();
        $reqHost = request()->getHost();
        $serverIp = (filter_var($reqHost, FILTER_VALIDATE_IP) && $reqHost !== '127.0.0.1')
            ? $reqHost
            : ($lab->ccc_ip ?: Setting::get('ccc_ip', $reqHost));
        $serverUrl = $serverIp;

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

    public function shareIndex(): Response
    {
        $text = (string) Setting::get('shared_clipboard', '');
        $updatedAt = (string) Setting::get('shared_clipboard_at', 'Never');
        $csrf = csrf_token();
        $escaped = htmlspecialchars($text, ENT_QUOTES, 'UTF-8');

        $html = <<<HTML
<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="csrf-token" content="{$csrf}">
    <title>XephyGuard Shared Clipboard</title>
    <style>
        * { box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; }
        .wrap { max-width: 860px; margin: 0 auto; }
        .card { background: #1e293b; border: 1px solid #334155; border-radius: 12px; padding: 24px; margin-bottom: 18px; }
        h1 { margin: 0 0 6px 0; font-size: 22px; display: flex; justify-content: space-between; align-items: center; }
        .sub { color: #94a3b8; font-size: 13px; margin-bottom: 16px; }
        textarea { width: 100%; min-height: 180px; background: #0f172a; color: #38bdf8; border: 1px solid #475569; border-radius: 8px; padding: 14px; font-family: Consolas, monospace; font-size: 14px; line-height: 1.5; resize: vertical; outline: none; }
        textarea:focus { border-color: #6366f1; }
        .actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 12px; align-items: center; }
        button { padding: 10px 18px; border-radius: 7px; border: none; font-weight: 600; font-size: 14px; cursor: pointer; transition: 0.15s; }
        .btn-save { background: #4f46e5; color: #fff; }
        .btn-save:hover { background: #4338ca; }
        .btn-copy { background: #10b981; color: #052e16; }
        .btn-copy:hover { background: #34d399; }
        .btn-clear { background: #334155; color: #cbd5e1; }
        .btn-clear:hover { background: #475569; }
        .status { font-size: 12px; color: #94a3b8; margin-left: auto; }
        .preset { background: #0f172a; border: 1px solid #334155; border-radius: 8px; padding: 12px; margin-top: 10px; display: flex; justify-content: space-between; align-items: center; gap: 12px; }
        .preset code { font-family: Consolas, monospace; font-size: 12px; color: #e2e8f0; word-break: break-all; }
        .preset button { padding: 6px 12px; font-size: 12px; white-space: nowrap; background: #334155; color: #fff; }
        .preset button:hover { background: #475569; }
        a { color: #818cf8; text-decoration: none; }
    </style>
</head>
<body>
<div class="wrap">
    <div class="card">
        <h1>
            <span>📋 XephyGuard Live Command &amp; Text Share</span>
            <a href="/downloads" style="font-size:13px;">⬇ Agent Downloads</a>
        </h1>
        <div class="sub">Paste any command or text here and click <b>Save &amp; Broadcast</b>. Any PC with this page open can copy it with one click.</div>

        <textarea id="box" placeholder="Paste PowerShell commands, URLs, or text here...">{$escaped}</textarea>

        <div class="actions">
            <button class="btn-copy" onclick="copyText(document.getElementById('box').value, this)">📋 Copy Text</button>
            <button class="btn-save" onclick="saveText()">💾 Save &amp; Broadcast</button>
            <button class="btn-clear" onclick="document.getElementById('box').value=''; saveText();">Clear</button>
            <span class="status" id="status">Last updated: {$updatedAt}</span>
        </div>
    </div>

    <div class="card">
        <div style="font-weight:600; font-size:15px; margin-bottom:8px;">⚡ Quick Test Commands (Click to Copy)</div>
        <div class="preset">
            <code>python -m http.server 11434 --bind 127.0.0.1</code>
            <button onclick="copyText('python -m http.server 11434 --bind 127.0.0.1', this)">📋 Copy (Test Port 11434)</button>
        </div>
        <div class="preset">
            <code>python -c "from http.server import BaseHTTPRequestHandler,HTTPServer; H=type('H',(BaseHTTPRequestHandler,),{'do_GET':lambda s:(s.send_response(200),s.send_header('Content-Type','application/json'),s.end_headers(),s.wfile.write(b'{\"data\":[{\"id\":\"llama3\"}]}'))}); HTTPServer(('127.0.0.1',8080),H).serve_forever()"</code>
            <button onclick="copyText(this.previousElementSibling.innerText, this)">📋 Copy (Test LLM Probe :8080)</button>
        </div>
    </div>
</div>

<script>
let lastSaved = document.getElementById('box').value;
let isTyping = false;
const box = document.getElementById('box');
box.addEventListener('focus', () => isTyping = true);
box.addEventListener('blur', () => isTyping = false);

function copyText(str, btn) {
    const done = () => {
        const old = btn.innerText;
        btn.innerText = '✅ Copied!';
        setTimeout(() => btn.innerText = old, 1500);
    };
    if (navigator.clipboard && window.isSecureContext) {
        navigator.clipboard.writeText(str).then(done);
    } else {
        const t = document.createElement('textarea');
        t.value = str;
        t.style.position = 'fixed';
        t.style.opacity = '0';
        document.body.appendChild(t);
        t.focus();
        t.select();
        document.execCommand('copy');
        document.body.removeChild(t);
        done();
    }
}

async function saveText() {
    const text = box.value;
    const res = await fetch('/share', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Accept': 'application/json',
            'X-CSRF-TOKEN': document.querySelector('meta[name="csrf-token"]').content
        },
        body: JSON.stringify({ text })
    });
    if (res.ok) {
        const d = await res.json();
        lastSaved = text;
        document.getElementById('status').innerText = 'Saved at ' + d.updated_at;
    }
}

setInterval(async () => {
    if (isTyping && box.value !== lastSaved) return;
    try {
        const res = await fetch('/share/raw');
        if (!res.ok) return;
        const d = await res.json();
        if (d.text !== lastSaved) {
            box.value = d.text;
            lastSaved = d.text;
            document.getElementById('status').innerText = 'Updated live at ' + d.updated_at;
        }
    } catch (e) {}
}, 2500);
</script>
</body>
</html>
HTML;

        return response($html, 200, ['Content-Type' => 'text/html; charset=utf-8']);
    }

    public function shareSave()
    {
        $text = (string) request()->input('text', '');
        $at = now()->format('H:i:s');
        Setting::set('shared_clipboard', mb_substr($text, 0, 65000));
        Setting::set('shared_clipboard_at', $at);

        return response()->json(['ok' => true, 'updated_at' => $at]);
    }

    public function shareRaw()
    {
        return response()->json([
            'text' => (string) Setting::get('shared_clipboard', ''),
            'updated_at' => (string) Setting::get('shared_clipboard_at', 'Never'),
        ]);
    }
}
