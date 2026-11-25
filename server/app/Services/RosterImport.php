<?php

declare(strict_types=1);

namespace App\Services;

use InvalidArgumentException;

/**
 * Parses a roster CSV (F54) into student rows, never guessing columns. With a header
 * (it names `login` or `student_id`) the columns are found by name (`student_id` or `id`,
 * `name`, `login`) and all three must be present; without one the order is id, name, login.
 * Rows that are too short, miss the id or login, or hold a value over 255 characters are
 * skipped with their line number and reason, so nothing fails silently (I-SERVER-31).
 */
final class RosterImport
{
    public const MAX_LEN = 255;

    /**
     * @param  list<array{student_number: string, name: string, login: string}>  $rows
     * @param  list<array{line: int, reason: string}>  $skipped
     */
    private function __construct(public readonly array $rows, public readonly array $skipped) {}

    /** @throws InvalidArgumentException when a header is present but lacks a required column */
    public static function parse(string $csv): self
    {
        $csv = preg_replace('/^\xEF\xBB\xBF/', '', $csv) ?? $csv;   // Excel writes a UTF-8 BOM
        $lines = preg_split('/\R/', trim($csv)) ?: [];
        $cells = array_map(fn (string $l) => array_map(fn ($c) => trim((string) $c), str_getcsv($l, ',', '"', '')), $lines);

        $header = array_map('strtolower', $cells[0] ?? []);
        $hasHeader = in_array('login', $header, true) || in_array('student_id', $header, true);
        $idx = $hasHeader ? self::headerIndex($header) : ['student_number' => 0, 'name' => 1, 'login' => 2];
        $need = max($idx) + 1;

        $rows = $skipped = [];
        foreach ($cells as $i => $r) {
            $line = $i + 1;
            if (($hasHeader && $i === 0) || $r === ['']) {
                continue;   // header or blank line
            }
            $reason = match (true) {
                count($r) < $need => "too few columns (need $need)",
                $r[$idx['student_number']] === '' => 'missing student id',
                $r[$idx['login']] === '' => 'missing login',
                max(array_map(fn ($k) => mb_strlen($r[$k]), $idx)) > self::MAX_LEN => 'value longer than '.self::MAX_LEN.' characters',
                default => null,
            };
            if ($reason !== null) {
                $skipped[] = ['line' => $line, 'reason' => $reason];

                continue;
            }
            $rows[] = ['student_number' => $r[$idx['student_number']], 'name' => $r[$idx['name']],
                'login' => mb_strtolower($r[$idx['login']])];
        }

        return new self($rows, $skipped);
    }

    /**
     * @param  list<string>  $header
     * @return array{student_number: int, name: int, login: int}
     */
    private static function headerIndex(array $header): array
    {
        $find = function (string ...$names) use ($header): ?int {
            foreach ($names as $n) {
                $i = array_search($n, $header, true);
                if ($i !== false) {
                    return $i;
                }
            }

            return null;
        };
        $idx = ['student_number' => $find('student_id', 'id'), 'name' => $find('name'), 'login' => $find('login')];
        $missing = array_keys(array_filter($idx, fn ($v) => $v === null));
        if ($missing !== []) {
            $labels = ['student_number' => 'student_id (or id)', 'name' => 'name', 'login' => 'login'];
            throw new InvalidArgumentException('Header is missing: '.implode(', ', array_map(fn ($k) => $labels[$k], $missing)).'.');
        }

        return $idx;
    }
}
