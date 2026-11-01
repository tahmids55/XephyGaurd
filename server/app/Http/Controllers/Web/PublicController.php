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

    public function download(string $file)
    {
        abort_unless(preg_match('/^[A-Za-z0-9._-]+\.(deb|rpm|msi|exe|sha256)$/', $file), 404);
        $path = storage_path('app/public/downloads/'.$file);
        abort_unless(is_file($path), 404, 'Installer not uploaded yet. Copy release artifacts to storage/app/public/downloads.');

        return response()->download($path);
    }
}
