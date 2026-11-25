<?php

declare(strict_types=1);

namespace App\Services\Ingest;

use App\Services\PrivacySettings;

/**
 * Server-side copy of the agent's title rule (S-29, DECISIONS #26/#27, ETHICS "Window titles"): a window title the
 * PC's effective `title_capture` does not allow is dropped before storage. `off` = no title; `app_only` = the title
 * may only be the application name; `full` = kept. Also drops the "(file)" part of paste/copy targets below `full`,
 * as the agent does (I-AGENT-9). Unknown levels count as `off`.
 */
final class TitleFilter
{
    public static function apply(string $type, array $data, string $capture): array
    {
        $capture = PrivacySettings::isTitleLevel($capture) ? $capture : 'off';
        if ($capture === 'full') {
            return $data;
        }
        if ($type === 'window_focus') {
            return self::object($data, $capture);
        }
        if ($type === 'rule_hit' && is_array($data['context'] ?? null)) {
            $data['context'] = self::object($data['context'], $capture);
        }
        if ($type === 'paste' || $type === 'copy') {
            foreach (['src', 'dst'] as $k) {
                if (is_string($data[$k] ?? null) && preg_match('/^(.*?) \(.*\)$/su', $data[$k], $m)) {
                    $data[$k] = $m[1];
                }
            }
        }

        return $data;
    }

    private static function object(array $o, string $capture): array
    {
        if (! array_key_exists('title', $o)) {
            return $o;
        }
        $app = is_string($o['app'] ?? null) ? $o['app'] : null;
        if ($capture === 'off' || $app === null || $o['title'] !== $app) {
            unset($o['title']);
        }

        return $o;
    }
}
