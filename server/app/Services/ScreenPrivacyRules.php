<?php

namespace App\Services;

use Illuminate\Support\Facades\Log;
use Symfony\Component\Yaml\Yaml;

/**
 * Windows the live screen view must never show (F43). Loaded from data/screen/privacy_rules.yaml and sent as
 * policy `screen.privacy_rules` (docs/PROTOCOL.md). The agent rejects a malformed list as a whole, so entries
 * that would break it are dropped here and logged.
 */
class ScreenPrivacyRules
{
    public const MAX_RULES = 128;

    public const MAX_STRING = 256;

    /** @var array{0: ?int, 1: array}|null mtime-keyed cache of the parsed file */
    private static ?array $cache = null;

    /** @return list<array{process?: string, title_regex?: string}> */
    public function rules(?string $path = null): array
    {
        $path ??= config('xephyguard.data_dir').'/screen/privacy_rules.yaml';
        if (! is_file($path)) {
            return [];
        }
        $mtime = filemtime($path) ?: null;
        $key = $path.'@'.$mtime;
        if (self::$cache !== null && self::$cache[0] === $key) {
            return self::$cache[1];
        }
        try {
            $doc = Yaml::parseFile($path);
        } catch (\Throwable $e) {
            Log::warning('screen privacy rules unreadable: '.$e->getMessage());

            return [];
        }
        $out = [];
        foreach (is_array($doc) ? ($doc['rules'] ?? []) : [] as $i => $r) {
            $rule = is_array($r) ? self::clean($r) : null;
            if ($rule === null) {
                Log::warning("screen privacy rule #$i skipped: needs process or title_regex (strings, valid regex, <= ".self::MAX_STRING.' bytes)');

                continue;
            }
            $out[] = $rule;
        }
        if (count($out) > self::MAX_RULES) {
            Log::warning('screen privacy rules truncated to '.self::MAX_RULES);
            $out = array_slice($out, 0, self::MAX_RULES);
        }
        self::$cache = [$key, $out];

        return $out;
    }

    /** @return array{process?: string, title_regex?: string}|null */
    public static function clean(array $r): ?array
    {
        $rule = [];
        foreach (['process', 'title_regex'] as $k) {
            if (! isset($r[$k])) {
                continue;
            }
            if (! is_string($r[$k]) || $r[$k] === '' || strlen($r[$k]) > self::MAX_STRING) {
                return null;
            }
            $rule[$k] = $r[$k];
        }
        if ($rule === []) {
            return null;
        }
        if (isset($rule['title_regex']) && @preg_match('~'.str_replace('~', '\~', $rule['title_regex']).'~i', '') === false) {
            return null;
        }

        return $rule;
    }
}
