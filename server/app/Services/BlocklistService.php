<?php

namespace App\Services;

use App\Models\BlocklistEntry;

/** Parses blocklist text (our format, hosts files, plain domain lists) into blocklist_entries. */
class BlocklistService
{
    public static function normalize(string $entry): ?array
    {
        $e = strtolower(trim($entry));
        $e = preg_replace('#^[a-z]+://#', '', $e);
        if ($e === '' || str_starts_with($e, '#')) {
            return null;
        }
        if (filter_var(explode('/', $e)[0], FILTER_VALIDATE_IP) && ! str_contains(trim($e, '/'), '/')) {
            return ['kind' => 'ip', 'value' => $e];
        }
        if (preg_match('#^[\d.:a-f]+/\d{1,3}$#', $e) && filter_var(explode('/', $e)[0], FILTER_VALIDATE_IP)) {
            return ['kind' => 'ip', 'value' => $e];
        }
        $e = preg_replace('#^\*\.#', '', $e);
        [$host, $path] = array_pad(explode('/', $e, 2), 2, null);
        $host = rtrim(explode(':', $host)[0], '.');
        if (! preg_match('/^([a-z0-9_]([a-z0-9_-]{0,61}[a-z0-9_])?\.)+[a-z0-9-]{2,63}$/', $host)) {
            return null;
        }
        $path = trim((string) $path, '/');

        return $path !== '' ? ['kind' => 'path', 'value' => "$host/$path"] : ['kind' => 'domain', 'value' => $host];
    }

    /** @return int number of new entries */
    public function import(string $category, string $text, string $source, ?int $userId = null): int
    {
        $n = 0;
        foreach (preg_split('/\R/', $text) as $line) {
            $line = trim(explode('#', $line, 2)[0]);
            $tokens = array_values(array_filter(preg_split('/\s+/', $line), fn ($t) => $t !== '' && $t[0] !== '['));
            if (! $tokens) {
                continue;
            }
            if (count($tokens) >= 2 && in_array($tokens[0], ['0.0.0.0', '127.0.0.1', '::'], true)) {
                array_shift($tokens);
            }
            $norm = self::normalize($tokens[0]);
            if (! $norm) {
                continue;
            }
            $created = BlocklistEntry::query()->firstOrCreate(['category' => $category, 'value' => $norm['value']],
                ['kind' => $norm['kind'], 'source' => $source, 'created_by' => $userId]);
            $n += $created->wasRecentlyCreated ? 1 : 0;
        }

        return $n;
    }

    /** Seeds from data/blocklists/*.txt. */
    public function importSeed(?string $dir = null): int
    {
        $dir ??= config('xephyguard.data_dir').'/blocklists';
        $map = ['ai_domains.txt' => 'ai', 'ide_ai_backends.txt' => 'ide', 'marketplaces.txt' => 'marketplace',
            'doh_dot.txt' => 'doh', 'doh_ips.txt' => 'doh', 'vpn_proxy.txt' => 'vpn'];
        $n = 0;
        foreach ($map as $file => $cat) {
            if (is_file("$dir/$file")) {
                $n += $this->import($cat, file_get_contents("$dir/$file"), 'seed');
            }
        }

        return $n;
    }
}
