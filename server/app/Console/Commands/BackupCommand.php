<?php

namespace App\Console\Commands;

use App\Services\CertificateAuthority;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use PharData;

class BackupCommand extends Command
{
    protected $signature = 'xephyguard:backup {--output= : Output archive path (.tar.gz)}';
    protected $description = 'Create a verified backup bundle (.tar.gz) of the database, CA material and manifest checksums';

    private const TABLES = [
        'users',
        'settings',
        'labs',
        'seats',
        'pcs',
        'enrollments',
        'enrollment_keys',
        'agents',
        'commands',
        'profiles',
        'timetable_entries',
        'blocklist_entries',
        'rules',
        'signature_versions',
        'lab_sessions',
        'session_pcs',
        'events',
        'alerts',
        'key_stats',
        'paste_events',
        'net_events',
        'students',
        'consents',
        'student_pc_assignments',
        'scores',
        'reports',
        'audit_log',
    ];

    public function handle(): int
    {
        $output = $this->option('output')
            ?: storage_path('app/backups/xephyguard-backup-'.now()->format('Ymd-His').'.tar.gz');

        @mkdir(dirname($output), 0700, true);
        $stageDir = sys_get_temp_dir().'/xg-backup-stage-'.bin2hex(random_bytes(6));
        @mkdir($stageDir.'/db', 0700, true);
        @mkdir($stageDir.'/ca', 0700, true);
        @mkdir($stageDir.'/meta', 0700, true);

        try {
            // 1. Export portable logical table snapshot + SQLite file if applicable
            $tablesDump = [];
            foreach (self::TABLES as $table) {
                if (Schema::hasTable($table)) {
                    $tablesDump[$table] = DB::table($table)->get()->map(fn ($r) => (array) $r)->all();
                }
            }
            file_put_contents(
                $stageDir.'/db/tables.json',
                json_encode($tablesDump, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES)
            );

            $driver = DB::connection()->getDriverName();
            if ($driver === 'sqlite') {
                $dbPath = DB::connection()->getDatabaseName();
                if ($dbPath && $dbPath !== ':memory:' && is_file($dbPath)) {
                    copy($dbPath, $stageDir.'/db/database.sqlite');
                }
            }

            // 2. Copy CA certificates and private keys
            $ca = CertificateAuthority::fromConfig();
            if (is_dir($ca->dir())) {
                foreach (scandir($ca->dir()) ?: [] as $entry) {
                    if ($entry === '.' || $entry === '..') {
                        continue;
                    }
                    $src = $ca->dir().'/'.$entry;
                    if (is_file($src)) {
                        copy($src, $stageDir.'/ca/'.$entry);
                    }
                }
            }

            // 3. Environment metadata
            file_put_contents($stageDir.'/meta/env_summary.json', json_encode([
                'app_env' => config('app.env'),
                'lab_name' => config('xephyguard.lab_name'),
                'ccc_ip' => config('xephyguard.ccc_ip'),
                'lab_subnet' => config('xephyguard.lab_subnet'),
                'network_mode' => config('xephyguard.network_mode'),
                'created_at' => now()->toIso8601String(),
            ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES));

            // 4. Build SHA-256 manifest
            $files = [];
            $iterator = new \RecursiveIteratorIterator(
                new \RecursiveDirectoryIterator($stageDir, \FilesystemIterator::SKIP_DOTS)
            );
            foreach ($iterator as $fileInfo) {
                if (! $fileInfo->isFile()) {
                    continue;
                }
                $relPath = str_replace('\\', '/', substr($fileInfo->getPathname(), strlen($stageDir) + 1));
                $files[$relPath] = [
                    'sha256' => hash_file('sha256', $fileInfo->getPathname()),
                    'size' => $fileInfo->getSize(),
                ];
            }
            ksort($files);

            $manifest = [
                'version' => 1,
                'created_at' => now()->toIso8601String(),
                'driver' => $driver,
                'files' => $files,
            ];
            file_put_contents(
                $stageDir.'/manifest.json',
                json_encode($manifest, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES)
            );

            // 5. Package into standard .tar.gz (POSIX tar + gzip stream)
            if (is_file($output)) {
                @unlink($output);
            }
            $tarPath = sys_get_temp_dir().'/xg-backup-'.bin2hex(random_bytes(6)).'.tar';

            $phar = new PharData($tarPath);
            $phar->buildFromDirectory($stageDir);
            unset($phar);

            if (str_ends_with(strtolower($output), '.gz') || str_ends_with(strtolower($output), '.tgz')) {
                $gzData = gzencode((string) file_get_contents($tarPath), 6);
                file_put_contents($output, $gzData);
                @unlink($tarPath);
            } else {
                rename($tarPath, $output);
            }
            @chmod($output, 0600);

            $this->info("Backup written to {$output} (".count($files).' files verified in manifest).');

            return self::SUCCESS;
        } finally {
            $this->removeDir($stageDir);
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
