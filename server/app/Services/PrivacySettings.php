<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Setting;

/**
 * Typed privacy settings with safe defaults (DECISIONS #26, ETHICS "Window titles").
 * The effective title capture is the stricter of the profile value and the per-mode setting,
 * ordered off < app_only < full, so neither source can widen collection on its own.
 */
final class PrivacySettings
{
    /** Strictness order: lower = collects less. */
    public const TITLE_LEVELS = ['off' => 0, 'app_only' => 1, 'full' => 2];

    /** Setting defaults per mode; an unknown mode gets the strictest value. */
    public const TITLE_DEFAULTS = ['always_on' => 'off', 'practice' => 'app_only', 'test' => 'app_only'];

    public static function isTitleLevel(mixed $v): bool
    {
        return is_string($v) && array_key_exists($v, self::TITLE_LEVELS);
    }

    /** The admin setting `privacy.title_capture.<mode>`, or its default; an invalid stored value counts as off. */
    public static function titleCaptureSetting(string $mode): string
    {
        $default = self::TITLE_DEFAULTS[$mode] ?? 'off';
        $v = Setting::get("privacy.title_capture.$mode", $default);

        return self::isTitleLevel($v) ? $v : 'off';
    }

    public static function stricter(string $a, string $b): string
    {
        $a = self::isTitleLevel($a) ? $a : 'off';
        $b = self::isTitleLevel($b) ? $b : 'off';

        return self::TITLE_LEVELS[$a] <= self::TITLE_LEVELS[$b] ? $a : $b;
    }

    /** What the agent receives: stricter of profile and setting; a missing profile value defers to the setting. */
    public static function effectiveTitleCapture(string $mode, mixed $profileValue): string
    {
        $setting = self::titleCaptureSetting($mode);
        if ($profileValue === null) {
            return $setting;
        }

        return self::isTitleLevel($profileValue) ? self::stricter($profileValue, $setting) : 'off';
    }

    /** @return array{enabled: bool, query: bool} URL logging in the proxy; query strings only with URLs on. */
    public static function logUrls(): array
    {
        $v = Setting::get('privacy.log_urls', []);
        $enabled = is_array($v) && (bool) ($v['enabled'] ?? false);

        return ['enabled' => $enabled, 'query' => $enabled && (bool) ($v['query'] ?? false)];
    }
}
