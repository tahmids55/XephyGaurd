<?php

namespace App\Services;

use RuntimeException;

/**
 * The CCC's internal CA. Created once by `php artisan xephyguard:ca:init` (called by install.sh).
 * The private key stays in ca_dir (0600) and is used to sign agent client certificates, the CCC
 * server certificate, the proxy's interception CA material and the signature bundles.
 */
class CertificateAuthority
{
    public function __construct(private readonly string $dir) {}

    public static function fromConfig(): self
    {
        return new self(config('xephyguard.ca_dir'));
    }

    public function dir(): string { return $this->dir; }
    public function certPath(): string { return $this->dir.'/ca.crt'; }
    public function keyPath(): string { return $this->dir.'/ca.key'; }
    public function exists(): bool { return is_file($this->certPath()) && is_file($this->keyPath()); }

    public function certPem(): string
    {
        $pem = @file_get_contents($this->certPath());
        if ($pem === false) {
            throw new RuntimeException('CA certificate not found; run php artisan xephyguard:ca:init');
        }

        return $pem;
    }

    public function fingerprint(): string
    {
        return openssl_x509_fingerprint($this->certPem(), 'sha256');
    }

    public function expiresAt(): ?\DateTimeImmutable
    {
        $info = openssl_x509_parse($this->certPem());

        return $info ? (new \DateTimeImmutable)->setTimestamp($info['validTo_time_t']) : null;
    }

    /** Creates the CA (EC P-256, 10 years). Refuses to overwrite an existing CA unless $force. */
    public function create(string $commonName, bool $force = false, int $days = 3650): void
    {
        if ($this->exists() && ! $force) {
            throw new RuntimeException('CA already exists');
        }
        @mkdir($this->dir, 0700, true);
        $conf = $this->opensslConfig();
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        $dn = ['commonName' => $commonName, 'organizationName' => 'XephyGuard'];
        $csr = openssl_csr_new($dn, $key, ['digest_alg' => 'sha256', 'config' => $conf]);
        $cert = openssl_csr_sign($csr, null, $key, $days, ['digest_alg' => 'sha256', 'config' => $conf,
            'x509_extensions' => 'v3_ca'], random_int(1, PHP_INT_MAX));
        openssl_x509_export($cert, $certPem);
        openssl_pkey_export($key, $keyPem, null, ['config' => $conf]);
        $this->writeSecret($this->keyPath(), $keyPem);
        file_put_contents($this->certPath(), $certPem);
        @unlink($conf);
    }

    /**
     * Signs an agent CSR as a client certificate (extendedKeyUsage=clientAuth).
     *
     * @return array{pem: string, serial: string, fingerprint: string, expires_at: \DateTimeImmutable}
     */
    public function signClientCsr(string $csrPem, int $days): array
    {
        $pub = @openssl_csr_get_public_key($csrPem);
        if ($pub === false) {
            throw new RuntimeException('invalid CSR');
        }
        $details = openssl_pkey_get_details($pub);
        $okType = ($details['type'] === OPENSSL_KEYTYPE_EC) || ($details['type'] === OPENSSL_KEYTYPE_RSA && $details['bits'] >= 2048);
        if (! $okType) {
            throw new RuntimeException('CSR key must be EC or RSA >= 2048');
        }

        return $this->sign($csrPem, $days, 'agent_ext');
    }

    /** Issues the CCC's own TLS server certificate (SAN = IPs + DNS names). */
    public function issueServerCert(array $ips, array $dnsNames, string $certOut, string $keyOut, int $days = 825): void
    {
        $alt = [];
        foreach (array_values($ips) as $i => $ip) {
            $alt[] = 'IP.'.($i + 1).' = '.$ip;
        }
        foreach (array_values($dnsNames) as $i => $d) {
            $alt[] = 'DNS.'.($i + 1).' = '.$d;
        }
        $conf = $this->opensslConfig(implode("\n", $alt));
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        $csr = openssl_csr_new(['commonName' => $dnsNames[0] ?? $ips[0], 'organizationName' => 'XephyGuard CCC'], $key,
            ['digest_alg' => 'sha256', 'config' => $conf]);
        openssl_csr_export($csr, $csrPem);
        $signed = $this->sign($csrPem, $days, 'server_ext', $conf);
        openssl_pkey_export($key, $keyPem, null, ['config' => $conf]);
        $this->writeSecret($keyOut, $keyPem);
        file_put_contents($certOut, $signed['pem'].$this->certPem());   // chain
        @unlink($conf);
    }

    /** Detached signature (base64) used for signature bundles and other agent-verified data. */
    public function signData(string $data): string
    {
        $key = openssl_pkey_get_private(file_get_contents($this->keyPath()));
        if (! openssl_sign($data, $sig, $key, OPENSSL_ALGO_SHA256)) {
            throw new RuntimeException('signing failed');
        }

        return base64_encode($sig);
    }

    public function verifyData(string $data, string $sigB64): bool
    {
        return openssl_verify($data, base64_decode($sigB64), openssl_pkey_get_public($this->certPem()), OPENSSL_ALGO_SHA256) === 1;
    }

    /** @return array{pem: string, serial: string, fingerprint: string, expires_at: \DateTimeImmutable} */
    private function sign(string $csrPem, int $days, string $ext, ?string $conf = null): array
    {
        $own = $conf === null;
        $conf ??= $this->opensslConfig();
        $serial = random_int(1, PHP_INT_MAX);
        $cert = openssl_csr_sign($csrPem, $this->certPem(), file_get_contents($this->keyPath()), $days,
            ['digest_alg' => 'sha256', 'config' => $conf, 'x509_extensions' => $ext], $serial);
        if ($own) {
            @unlink($conf);
        }
        if ($cert === false) {
            throw new RuntimeException('certificate signing failed: '.openssl_error_string());
        }
        openssl_x509_export($cert, $pem);
        $info = openssl_x509_parse($pem);

        return [
            'pem' => $pem,
            'serial' => (string) $serial,
            'fingerprint' => openssl_x509_fingerprint($pem, 'sha256'),
            'expires_at' => (new \DateTimeImmutable)->setTimestamp($info['validTo_time_t']),
        ];
    }

    public function opensslConfig(string $altNames = 'DNS.1 = localhost'): string
    {
        $path = tempnam(sys_get_temp_dir(), 'xgssl');
        file_put_contents($path, <<<CONF
        [ req ]
        default_bits = 2048
        distinguished_name = req_dn
        [ req_dn ]
        [ v3_ca ]
        basicConstraints = critical,CA:true,pathlen:0
        keyUsage = critical,keyCertSign,cRLSign,digitalSignature
        subjectKeyIdentifier = hash
        [ agent_ext ]
        basicConstraints = critical,CA:false
        keyUsage = critical,digitalSignature,keyAgreement
        extendedKeyUsage = clientAuth
        subjectKeyIdentifier = hash
        authorityKeyIdentifier = keyid
        [ server_ext ]
        basicConstraints = critical,CA:false
        keyUsage = critical,digitalSignature,keyAgreement,keyEncipherment
        extendedKeyUsage = serverAuth
        subjectKeyIdentifier = hash
        authorityKeyIdentifier = keyid
        subjectAltName = @alt
        [ alt ]
        $altNames
        CONF);

        return $path;
    }

    private function writeSecret(string $path, string $content): void
    {
        $old = umask(0077);
        file_put_contents($path, $content);
        chmod($path, 0600);
        umask($old);
    }
}
