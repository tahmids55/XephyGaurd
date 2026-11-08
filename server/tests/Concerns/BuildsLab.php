<?php

namespace Tests\Concerns;

use App\Models\Agent;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\User;
use App\Services\Bootstrap;
use App\Services\CertificateAuthority;
use Illuminate\Support\Str;

trait BuildsLab
{
    protected string $caDir;

    protected function bootLab(): Lab
    {
        $this->caDir = sys_get_temp_dir().'/xg-ca-'.Str::random(8);
        $netState = sys_get_temp_dir().'/xg-net-'.Str::random(8).'/state.json';
        config(['xephyguard.ca_dir' => $this->caDir, 'xephyguard.net_state_path' => $netState,
            'xephyguard.data_dir' => base_path('../data')]);
        $this->app->forgetInstance(CertificateAuthority::class);
        CertificateAuthority::fromConfig()->create('Test CA');
        app(Bootstrap::class)->run();
        User::create(['name' => 'Owner', 'email' => 'owner@lab.test', 'password' => 'correct-horse-battery', 'role' => 'admin']);

        return Lab::current();
    }

    protected function user(string $role = 'admin'): User
    {
        return User::create(['name' => ucfirst($role), 'email' => $role.Str::random(4).'@lab.test',
            'password' => 'correct-horse-battery', 'role' => $role]);
    }

    /** Creates an enrolled PC + agent and returns [pc, token, cert_pem]. */
    protected function enrolledPc(string $ip = '192.168.10.21', string $name = 'PC-01'): array
    {
        $ca = app(CertificateAuthority::class);
        $conf = $ca->opensslConfig();
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        $csr = openssl_csr_new(['commonName' => $name], $key, ['digest_alg' => 'sha256', 'config' => $conf]);
        @unlink($conf);
        openssl_csr_export($csr, $csrPem);
        $signed = $ca->signClientCsr($csrPem, 30);
        $pc = Pc::create(['lab_id' => Lab::current()->id, 'name' => $name, 'hostname' => strtolower($name), 'ip' => $ip,
            'os' => 'Linux', 'macs' => ['aa:bb:cc:00:00:'.substr(md5($name), 0, 2)]]);
        $token = Str::random(64);
        Agent::create(['pc_id' => $pc->id, 'token_hash' => hash('sha256', $token), 'cert_fingerprint' => $signed['fingerprint'],
            'cert_expires_at' => $signed['expires_at']]);

        return [$pc, $token, $signed['pem']];
    }

    protected function agentHeaders(string $token, string $certPem): array
    {
        return ['Authorization' => "Bearer $token", 'X-SSL-Client-Verify' => 'SUCCESS',
            'X-SSL-Client-Cert' => rawurlencode($certPem), 'Accept' => 'application/json'];
    }

    protected function profile(string $name): Profile
    {
        return Profile::query()->where('name', $name)->firstOrFail();
    }

    protected function netState(): array
    {
        return json_decode(file_get_contents(config('xephyguard.net_state_path')), true);
    }
}
