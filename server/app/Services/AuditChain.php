<?php

declare(strict_types=1);

namespace App\Services;

/**
 * Hash chain over audit_log (DECISIONS #29). Each row's prev_hash = hash() of the row before it
 * (by id); the first row holds GENESIS. hash() covers the row's own prev_hash, so it chains.
 * Works on raw query-builder rows so writer, migration and verifier see the same stored values.
 */
final class AuditChain
{
    public const GENESIS = '0000000000000000000000000000000000000000000000000000000000000000';

    public static function hash(object $row): string
    {
        $details = $row->details === null ? null : json_decode((string) $row->details, true, 512, JSON_THROW_ON_ERROR);

        return hash('sha256', json_encode([
            (int) $row->id,
            $row->prev_hash === null ? null : (string) $row->prev_hash,
            $row->user_id === null ? null : (int) $row->user_id,
            (string) $row->action,
            $row->subject_type === null ? null : (string) $row->subject_type,
            $row->subject_id === null ? null : (int) $row->subject_id,
            self::sortKeys($details),
            $row->ip === null ? null : (string) $row->ip,
            $row->created_at === null ? null : (string) $row->created_at,
        ], JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_THROW_ON_ERROR));
    }

    private static function sortKeys(mixed $v): mixed
    {
        if (! is_array($v)) {
            return $v;
        }
        if (! array_is_list($v)) {
            ksort($v, SORT_STRING);
        }

        return array_map(self::sortKeys(...), $v);
    }
}
