<?php

namespace App\Console\Commands;

use App\Models\Lab;
use App\Services\Bootstrap;
use App\Services\CertificateAuthority;
use App\Services\Doctor;
use App\Services\NetworkPolicyWriter;
use App\Services\SignatureCompiler;
use Illuminate\Console\Command;

class CaInit extends Command
{
    protected $signature = 'xephyguard:ca:init {--cn=XephyGuard Lab CA} {--server-ip=*} {--server-dns=*} {--force}';
    protected $description = 'Create the internal CA (if missing) and issue the CCC server certificate';

    public function handle(): int
    {
        $ca = CertificateAuthority::fromConfig();
        $force = (bool) $this->option('force');
        if (! $ca->exists() || $force) {
            $ca->create($this->option('cn'), $force);
            $this->info('CA created in '.$ca->dir());
        } else {
            $this->line('CA already exists in '.$ca->dir());
        }
        $ips = $this->option('server-ip') ?: [config('xephyguard.ccc_ip')];
        $dns = $this->option('server-dns') ?: ['xephyguard-ccc', 'localhost'];
        $ca->issueServerCert($ips, $dns, $ca->dir().'/server.crt', $ca->dir().'/server.key');
        $this->info('Server certificate issued for '.implode(', ', array_merge($ips, $dns)));

        $ca->issueSigningCert($force);
        $this->info('Signature-signing certificate issued in '.$ca->signingCertPath());

        $ca->issueInterceptionCa(force: $force);
        $this->info('Name-constrained proxy interception Sub-CA issued in '.$ca->interceptionCaCertPath());

        $this->line('CA fingerprint (SHA-256): '.$ca->fingerprint());

        return self::SUCCESS;
    }
}
