<?php

namespace App\Console\Commands;

use App\Services\CertificateAuthority;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use PharData;

class RestoreCommand extends Command
{
    protected $signature = 'xephyguard:restore {archive : Path to the backup .tar.gz archive} {--verify-only : Only verify manifest SHA-256 checksums and archive integrity without modifying active state}';
    protected $description = 'Verify (or restore) a XephyGuard backup bundle created by xephyguard:backup';

    public function handle(): int
    {
        $archive = (string) $this->argument('archive');
        if (! is_file($archive)) {
            $this->error("Archive not found: {$archive}");

            return self::FAILURE;
        }

        $extractDir = sys_get_temp_dir().'/xg-restore-'.bin2hex(random_bytes(6));
        $tempTar = null;
        @mkdir($extractDir, 0700, true);

        try {
            try {
                $rawBytes = (string) file_get_contents($archive);
                if (str_starts_with($rawBytes, "\x1f\x8b")) {
                    $decompressed = @gzdecode($rawBytes);
                    if ($decompressed === false) {
                        $this->error('Failed to gunzip archive.');

                        return self::FAILURE;
                    }
                    $tempTar = sys_get_temp_dir().'/xg-restore-'.bin2hex(random_bytes(6)).'.tar';
                    file_put_contents($tempTar, $decompressed);
                    $phar = new PharData($tempTar);
                } else {
                    $phar = new PharData($archive);
                }
                $phar->extractTo($extractDir, null, true);
                unset($phar);
            } catch (\Throwable $e) {
                $this->error('Failed to extract archive: '.$e->getMessage());

                return self::FAILURE;
            }

            $manifestPath = $extractDir.'/manifest.json';
            if (! is_file($manifestPath)) {
                $this->error('Invalid backup archive: manifest.json is missing.');

                return self::FAILURE;
            }

            $manifest = json_decode((string) file_get_contents($manifestPath), true);
            if (! is_array($manifest) || empty($manifest['files']) || ! is_array($manifest['files'])) {
                $this->error('Invalid backup archive: manifest.json is malformed.');

                return self::FAILURE;
            }

            foreach ($manifest['files'] as $relPath => $meta) {
                if (str_contains($relPath, '..')) {
                    $this->error("Unsafe path in manifest: {$relPath}");

                    return self::FAILURE;
                }
                $fullPath = $extractDir.'/'.$relPath;
                if (! is_file($fullPath)) {
                    $this->error("Missing file listed in manifest: {$relPath}");

                    return self::FAILURE;
                }
                $actualHash = hash_file('sha256', $fullPath);
                $expectedHash = (string) ($meta['sha256'] ?? '');
                if (! hash_equals($expectedHash, $actualHash)) {
                    $this->error("SHA-256 mismatch for {$relPath}: expected {$expectedHash}, got {$actualHash}");

                    return self::FAILURE;
                }
            }

            // Dry-run integrity checks on extracted DB snapshot and CA cert
            $tablesJsonPath = $extractDir.'/db/tables.json';
            if (! is_file($tablesJsonPath)) {
                $this->error('Backup archive is missing db/tables.json.');

                return self::FAILURE;
            }
            $tablesData = json_decode((string) file_get_contents($tablesJsonPath), true);
            if (! is_array($tablesData)) {
                $this->error('db/tables.json is not valid JSON.');

                return self::FAILURE;
            }

            $caCertPath = $extractDir.'/ca/ca.crt';
            if (is_file($caCertPath) && @openssl_x509_parse((string) file_get_contents($caCertPath)) === false) {
                $this->error('ca/ca.crt failed X.509 parsing check.');

                return self::FAILURE;
            }

            $fileCount = count($manifest['files']);
            if ($this->option('verify-only')) {
                $this->info("Verified backup archive ({$fileCount} files, SHA-256 checksums and X.509/DB structure OK).");

                return self::SUCCESS;
            }

            // Full restore: CA directory + Database tables
            $caDir = CertificateAuthority::fromConfig()->dir();
            @mkdir($caDir, 0700, true);
            if (is_dir($extractDir.'/ca')) {
                foreach (scandir($extractDir.'/ca') ?: [] as $entry) {
                    if ($entry === '.' || $entry === '..') {
                        continue;
                    }
                    $src = $extractDir.'/ca/'.$entry;
                    $dst = $caDir.'/'.$entry;
                    if (is_file($src)) {
                        copy($src, $dst);
                        if (str_ends_with($entry, '.key') || str_ends_with($entry, '.pem')) {
                            @chmod($dst, 0600);
                        }
                    }
                }
            }

            DB::transaction(function () use ($tablesData) {
                Schema::disableForeignKeyConstraints();
                foreach ($tablesData as $table => $rows) {
                    if (! Schema::hasTable($table) || ! is_array($rows)) {
                        continue;
                    }
                    DB::table($table)->delete();
                    foreach (array_chunk($rows, 200) as $chunk) {
                        DB::table($table)->insert($chunk);
                    }
                }
                Schema::enableForeignKeyConstraints();
            });

            $this->info("Restored backup archive ({$fileCount} files).");

            return self::SUCCESS;
        } finally {
            if ($tempTar !== null && is_file($tempTar)) {
                @unlink($tempTar);
            }
            $this->removeDir($extractDir);
        }
    }

    private function removeDir(string $dir): void
    {
        if (! is_dir($dir)) {
            return;
        }
        $it = new \RecursiveIteratorIterator(
            new \RecursiveDirectoryIterator($dir, \FilesystemIterator::SKIP_DOTS),
            \RecursiveIteratorIterator::CHILD_FIRST
        );
        foreach ($it as $item) {
            $item->isDir() ? @rmdir($item->getPathname()) : @unlink($item->getPathname());
        }
        @rmdir($dir);
    }
}
