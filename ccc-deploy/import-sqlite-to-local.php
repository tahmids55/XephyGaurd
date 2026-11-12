<?php

declare(strict_types=1);

/**
 * Copies application state from an existing SQLite database into a migrated MySQL database.
 * Intended to run inside the CCC web container with env DB_* variables already loaded.
 */
$sqlitePath = $argv[1] ?? '/tmp/import.sqlite';
if (! is_file($sqlitePath)) {
    fwrite(STDERR, "SQLite file not found: {$sqlitePath}\n");
    exit(1);
}

$tzName = getenv('APP_TIMEZONE') ?: (date_default_timezone_get() ?: 'UTC');
$tz = new DateTimeZone($tzName);
$offsetSec = $tz->getOffset(new DateTimeImmutable('now', new DateTimeZone('UTC')));
$sign = $offsetSec >= 0 ? '+' : '-';
$absSec = abs($offsetSec);
$mysqlOffset = sprintf('%s%02d:%02d', $sign, intdiv($absSec, 3600), intdiv($absSec % 3600, 60));
$src = new PDO("sqlite:{$sqlitePath}");
$src->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

$dbHost = getenv('DB_HOST') ?: '127.0.0.1';
$dbPort = getenv('DB_PORT') ?: '3306';
$dbName = getenv('DB_DATABASE') ?: 'xephyguard';
$dbUser = getenv('DB_USERNAME') ?: 'xephyguard';
$dbPass = getenv('DB_PASSWORD') ?: '';

$dst = new PDO(
    "mysql:host={$dbHost};port={$dbPort};dbname={$dbName};charset=utf8mb4",
    $dbUser,
    $dbPass,
    [
        PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
        PDO::MYSQL_ATTR_INIT_COMMAND => "SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci, time_zone = '{$mysqlOffset}'",
    ]
);

$skipTables = ['migrations', 'cache', 'cache_locks', 'sessions', 'jobs', 'failed_jobs', 'job_batches'];
$tables = $src->query("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name")
    ->fetchAll(PDO::FETCH_COLUMN);

function normalizeTemporalValue(mixed $value, int $precision, DateTimeZone $tz): ?string
{
    if ($value === null || $value === '') {
        return null;
    }
    $dt = (new DateTimeImmutable(trim((string) $value), $tz))->setTimezone($tz);

    return $precision > 0 ? substr($dt->format('Y-m-d H:i:s.u'), 0, 20 + $precision) : $dt->format('Y-m-d H:i:s');
}

$colStmt = $dst->prepare(
    'SELECT COLUMN_NAME, DATA_TYPE, COALESCE(DATETIME_PRECISION, 0) AS PREC
     FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = ? AND TABLE_NAME = ?
     ORDER BY ORDINAL_POSITION'
);

$dst->exec('SET FOREIGN_KEY_CHECKS = 0');

foreach ($tables as $table) {
    if (in_array($table, $skipTables, true)) {
        continue;
    }

    $colStmt->execute([$dbName, $table]);
    $mysqlCols = [];
    foreach ($colStmt->fetchAll(PDO::FETCH_ASSOC) as $c) {
        $mysqlCols[$c['COLUMN_NAME']] = [
            'type' => strtolower($c['DATA_TYPE']),
            'prec' => (int) $c['PREC'],
        ];
    }
    if ($mysqlCols === []) {
        echo "skip {$table} (not in target schema)\n";
        continue;
    }

    $sqliteCols = array_column($src->query("PRAGMA table_info(\"{$table}\")")->fetchAll(PDO::FETCH_ASSOC), 'name');
    $cols = array_values(array_intersect($sqliteCols, array_keys($mysqlCols)));
    if ($cols === []) {
        continue;
    }

    $dst->exec("TRUNCATE TABLE `{$table}`");

    $selectSql = 'SELECT '.implode(', ', array_map(fn ($c) => "\"{$c}\"", $cols))." FROM \"{$table}\"";
    $rows = $src->query($selectSql)->fetchAll(PDO::FETCH_ASSOC);
    $srcCount = count($rows);

    if ($srcCount > 0) {
        $colList = implode(', ', array_map(fn ($c) => "`{$c}`", $cols));
        $rowPlaceholder = '('.implode(', ', array_fill(0, count($cols), '?')).')';
        $dst->beginTransaction();
        foreach (array_chunk($rows, 200) as $chunk) {
            $placeholders = implode(', ', array_fill(0, count($chunk), $rowPlaceholder));
            $insertSql = "INSERT INTO `{$table}` ({$colList}) VALUES {$placeholders}";
            $params = [];
            foreach ($chunk as $row) {
                foreach ($cols as $col) {
                    $val = $row[$col];
                    $meta = $mysqlCols[$col];
                    if (in_array($meta['type'], ['timestamp', 'datetime'], true)) {
                        $val = normalizeTemporalValue($val, $meta['prec'], $tz);
                    }
                    $params[] = $val;
                }
            }
            $dst->prepare($insertSql)->execute($params);
        }
        $dst->commit();
    }

    $dstCount = (int) $dst->query("SELECT COUNT(*) FROM `{$table}`")->fetchColumn();
    echo sprintf("%-24s src=%-5d dst=%-5d\n", $table, $srcCount, $dstCount);
    if ($dstCount !== $srcCount) {
        fwrite(STDERR, "Row count mismatch for {$table}!\n");
        exit(2);
    }
}

$dst->exec('SET FOREIGN_KEY_CHECKS = 1');
echo "SQLite -> MySQL import complete.\n";
