<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Setting;

/**
 * The student notice (F33, F98). The admin text says what the lab is about; the sentences about screen
 * access are generated from the features that are really on, so a custom text cannot leave them out
 * (S-25, I-SERVER-12, ETHICS "Transparency"). The same text goes to the agent and to /notice.
 */
class NoticeText
{
    public const BASE = 'This computer is monitored by XephyGuard during lab time. We record which programs and '.
        'websites are used and typing/paste statistics. We never record what you type, your clipboard text or your passwords.';

    /** The screen sentence of the old default text; removed from stored texts so it is not stated twice. */
    private const LEGACY_SCREEN = 'Lab staff can view this screen live and may save a screenshot as evidence; every view is logged.';

    /**
     * Live view needs a screen-gateway signing secret (no secret, no viewer token). A screenshot can only be
     * saved from an open audited live view (S-12), so it is on exactly when live view is.
     *
     * @return array{live_view: bool, screenshots: bool}
     */
    public static function features(): array
    {
        $live = (string) config('services.screen_gateway.secret', '') !== '';

        return ['live_view' => $live, 'screenshots' => $live];
    }

    /** The admin's own text (or the default), without the generated part. */
    public static function adminText(): string
    {
        $text = trim(str_replace(self::LEGACY_SCREEN, '', (string) Setting::get('notice_text', '')));

        return $text !== '' ? $text : self::BASE;
    }

    public static function featureSentences(): string
    {
        $f = self::features();
        if (! $f['live_view']) {
            return 'Live screen viewing is turned off on this lab.';
        }
        $s = 'Lab staff can view this screen live; it is not recorded and every view is logged.';
        if ($f['screenshots']) {
            $s .= ' An instructor watching live may save a screenshot as evidence; saving and viewing it are logged.';
        }

        return $s;
    }

    public static function text(): string
    {
        return self::adminText().' '.self::featureSentences();
    }
}
