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

    public function signingCertPath(): string { return $this->dir.'/signing.crt'; }
    public function signingKeyPath(): string { return $this->dir.'/signing.key'; }
    public function hasSigningCert(): bool { return is_file($this->signingCertPath()) && is_file($this->signingKeyPath()); }

    public function signingCertPem(): ?string
    {
        if (! $this->hasSigningCert()) {
            return null;
        }
        $pem = @file_get_contents($this->signingCertPath());

        return $pem !== false ? $pem : null;
    }

    public function interceptionCaCertPath(): string { return $this->dir.'/interception-ca.crt'; }
    public function interceptionCaKeyPath(): string { return $this->dir.'/interception-ca.key'; }
    public function mitmproxyPemPath(): string { return $this->dir.'/mitmproxy-ca.pem'; }
    public function hasInterceptionCa(): bool { return is_file($this->interceptionCaCertPath()) && is_file($this->interceptionCaKeyPath()); }

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
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        $conf = $this->opensslConfig();
        $dn = ['commonName' => $commonName, 'organizationName' => 'XephyGuard'];
        $csr = openssl_csr_new($dn, $key, ['digest_alg' => 'sha256', 'config' => $conf]);
        $cert = openssl_csr_sign($csr, null, $key, $days, ['digest_alg' => 'sha256', 'config' => $conf,
            'x509_extensions' => 'v3_ca'], random_int(1, PHP_INT_MAX));
        openssl_x509_export($cert, $certPem);
        openssl_pkey_export($key, $keyPem);
        $this->writeSecret($this->keyPath(), $keyPem);
        file_put_contents($this->certPath(), $certPem);
        @unlink($conf);
    }

    /**
     * Issues a dedicated EC P-256 leaf certificate for signing detection signature bundles
     * (basicConstraints=CA:false, keyUsage=digitalSignature, extendedKeyUsage=codeSigning).
     */
    public function issueSigningCert(bool $force = false, int $days = 1825): void
    {
        if ($this->hasSigningCert() && ! $force) {
            return;
        }
        if (! $this->exists()) {
            throw new RuntimeException('CA certificate not found; run php artisan xephyguard:ca:init');
        }
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        $conf = $this->opensslConfig();
        $csr = openssl_csr_new(
            ['commonName' => 'XephyGuard Signature Signer', 'organizationName' => 'XephyGuard'],
            $key,
            ['digest_alg' => 'sha256', 'config' => $conf]
        );
        openssl_csr_export($csr, $csrPem);
        $signed = $this->sign($csrPem, $days, 'signing_ext', $conf);
        openssl_pkey_export($key, $keyPem);
        $this->writeSecret($this->signingKeyPath(), $keyPem);
        file_put_contents($this->signingCertPath(), $signed['pem']);
        @unlink($conf);
    }

    /**
     * Issues a name-constrained sub-CA for mitmproxy search inspection.
     * Cryptographically restricted via X.509 Name Constraints to $permittedDomains and
     * explicitly excludes all IPv4 and IPv6 address SANs so it can never spoof the CCC IP.
     */
    public function issueInterceptionCa(
        array $permittedDomains = ['.google.com', 'google.com', '.bing.com', 'bing.com', '.duckduckgo.com', 'duckduckgo.com'],
        bool $force = false,
        int $days = 1095
    ): void {
        if ($this->hasInterceptionCa() && ! $force) {
            return;
        }
        if (! $this->exists()) {
            throw new RuntimeException('CA certificate not found; run php artisan xephyguard:ca:init');
        }
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        $conf = $this->opensslConfig('DNS.1 = localhost', $permittedDomains);
        $csr = openssl_csr_new(
            ['commonName' => 'XephyGuard Search Inspection Sub-CA', 'organizationName' => 'XephyGuard Proxy'],
            $key,
            ['digest_alg' => 'sha256', 'config' => $conf]
        );
        openssl_csr_export($csr, $csrPem);
        $signed = $this->sign($csrPem, $days, 'interception_ca_ext', $conf);
        openssl_pkey_export($key, $keyPem);
        $this->writeSecret($this->interceptionCaKeyPath(), $keyPem);
        file_put_contents($this->interceptionCaCertPath(), $signed['pem']);
        $this->writeSecret($this->mitmproxyPemPath(), $keyPem.$signed['pem'].$this->certPem());
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
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        $alt = [];
        foreach (array_values($ips) as $i => $ip) {
            $alt[] = 'IP.'.($i + 1).' = '.$ip;
        }
        foreach (array_values($dnsNames) as $i => $d) {
            $alt[] = 'DNS.'.($i + 1).' = '.$d;
        }
        $conf = $this->opensslConfig(implode("\n", $alt));
        $csr = openssl_csr_new(['commonName' => $dnsNames[0] ?? $ips[0], 'organizationName' => 'XephyGuard CCC'], $key,
            ['digest_alg' => 'sha256', 'config' => $conf]);
        openssl_csr_export($csr, $csrPem);
        $signed = $this->sign($csrPem, $days, 'server_ext', $conf);
        openssl_pkey_export($key, $keyPem);
        $this->writeSecret($keyOut, $keyPem);
        file_put_contents($certOut, $signed['pem'].$this->certPem());   // chain
        @unlink($conf);
    }

    /** Detached signature (base64) used for signature bundles and other agent-verified data. */
    public function signData(string $data): string
    {
        $keyFile = $this->hasSigningCert() ? $this->signingKeyPath() : $this->keyPath();
        $key = openssl_pkey_get_private(file_get_contents($keyFile));
        if (! openssl_sign($data, $sig, $key, OPENSSL_ALGO_SHA256)) {
            throw new RuntimeException('signing failed');
        }

        return base64_encode($sig);
    }

    public function verifyData(string $data, string $sigB64, ?string $signerCertPem = null): bool
    {
        $rawSig = base64_decode($sigB64, true);
        if ($rawSig === false) {
            return false;
        }
        $caPub = @openssl_pkey_get_public($this->certPem());
        if ($caPub === false) {
            return false;
        }

        $signerPem = $signerCertPem ?? $this->signingCertPem();
        if ($signerPem !== null && trim($signerPem) !== '') {
            if (@openssl_x509_verify($signerPem, $caPub) !== 1) {
                return false;
            }
            $info = @openssl_x509_parse($signerPem);
            if (! is_array($info)) {
                return false;
            }
            $now = time();
            if (($info['validFrom_time_t'] ?? PHP_INT_MAX) > $now || ($info['validTo_time_t'] ?? 0) < $now) {
                return false;
            }
            $bc = strtoupper((string) ($info['extensions']['basicConstraints'] ?? ''));
            if (! str_contains($bc, 'CA:FALSE')) {
                return false;
            }
            $eku = strtolower((string) ($info['extensions']['extendedKeyUsage'] ?? ''));
            if (! str_contains($eku, 'code signing') && ! str_contains($eku, 'codesigning')) {
                return false;
            }
            $signerPub = @openssl_pkey_get_public($signerPem);
            if ($signerPub === false) {
                return false;
            }
            if (@openssl_verify($data, $rawSig, $signerPub, OPENSSL_ALGO_SHA256) === 1) {
                return true;
            }
            // If no explicit $signerCertPem was forced, allow fallback verification against root CA for legacy bundles
            if ($signerCertPem !== null) {
                return false;
            }
        }

        return @openssl_verify($data, $rawSig, $caPub, OPENSSL_ALGO_SHA256) === 1;
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

    private function opensslConfig(
        string $altNames = 'DNS.1 = localhost',
        array $permittedDomains = ['.google.com', 'google.com', '.bing.com', 'bing.com', '.duckduckgo.com', 'duckduckgo.com']
    ): string {
        $ncLines = [];
        foreach (array_values($permittedDomains) as $i => $domain) {
            $ncLines[] = 'permitted;DNS.'.($i + 1).' = '.$domain;
        }
        $ncLines[] = 'excluded;IP.1 = 0.0.0.0/0.0.0.0';
        $ncLines[] = 'excluded;IP.2 = 0:0:0:0:0:0:0:0/0:0:0:0:0:0:0:0';
        $ncSect = implode("\n", $ncLines);

        $path = tempnam(sys_get_temp_dir(), 'xgssl');
        file_put_contents($path, <<<CONF
        [ req ]
        distinguished_name = req_dn
        [ req_dn ]
        [ v3_ca ]
        basicConstraints = critical,CA:true,pathlen:1
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
        [ signing_ext ]
        basicConstraints = critical,CA:false
        keyUsage = critical,digitalSignature
        extendedKeyUsage = critical,codeSigning
        subjectKeyIdentifier = hash
        authorityKeyIdentifier = keyid
        [ interception_ca_ext ]
        basicConstraints = critical,CA:true,pathlen:0
        keyUsage = critical,keyCertSign,cRLSign,digitalSignature
        subjectKeyIdentifier = hash
        authorityKeyIdentifier = keyid
        nameConstraints = critical,@nc_sect
        [ nc_sect ]
        $ncSect
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
