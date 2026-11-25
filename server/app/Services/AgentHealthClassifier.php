<?php

declare(strict_types=1);

namespace App\Services;

use DateTimeInterface;

class AgentHealthClassifier
{
    public const STATUS_OK = 'OK';

    public const STATUS_UNAVAILABLE = 'AGENT_UNAVAILABLE';

    public const STATUS_TAMPERING = 'SUSPECTED_TAMPERING';

    /**
     * Classifies the health state of an agent PC.
     *
     * @param  array<string, mixed>|object  $pc
     * @param  array<int, array<string, mixed>|object>  $labSnapshot
     * @param  DateTimeInterface|int|null  $now
     * @param  int|null  $timeoutS  Optional explicit heartbeat timeout override in seconds
     * @return string 'OK' | 'AGENT_UNAVAILABLE' | 'SUSPECTED_TAMPERING'
     */
    public static function classify($pc, array $labSnapshot = [], $now = null, ?int $timeoutS = null): string
    {
        return self::assess($pc, $labSnapshot, $now, $timeoutS)['status'];
    }

    /**
     * Same decision as classify(), plus the human-readable reasons behind it.
     *
     * @param  array<string, mixed>|object  $pc
     * @param  array<int, array<string, mixed>|object>  $labSnapshot
     * @param  DateTimeInterface|int|null  $now
     * @return array{status: string, reasons: list<string>}
     */
    public static function assess($pc, array $labSnapshot = [], $now = null, ?int $timeoutS = null): array
    {
        $nowTimestamp = self::toTimestamp($now) ?? time();
        $pcData = (array) $pc;
        $threshold = self::resolveTimeout($pcData, $timeoutS);

        $lastHeartbeat = self::toTimestamp($pcData['last_heartbeat_at'] ?? $pcData['last_seen_at'] ?? null);

        // If never seen or no heartbeat recorded, it's unavailable
        if ($lastHeartbeat === null) {
            $elapsed = PHP_INT_MAX;
        } else {
            $elapsed = max(0, $nowTimestamp - $lastHeartbeat);
        }

        // Within 3x heartbeat window (default 45s for 15s heartbeat) keeps PC OK
        if ($elapsed <= $threshold) {
            return ['status' => self::STATUS_OK, 'reasons' => ["last heartbeat {$elapsed}s ago, within the {$threshold}s limit"]];
        }

        $reasons = [$lastHeartbeat === null
            ? "no heartbeat recorded (limit {$threshold}s)"
            : "no heartbeat for {$elapsed}s (limit {$threshold}s)"];

        // Condition 1: Unavailable longer than 5 minutes (> 300s)
        $longerThan5Min = $elapsed > 300;
        $reasons[] = $longerThan5Min ? 'silent for more than 5 minutes' : 'silent for 5 minutes or less';

        // Condition 2: At least 80% of the other PCs in the same lab are still connected
        $pcId = $pcData['id'] ?? null;
        $otherPcs = [];
        foreach ($labSnapshot as $other) {
            $otherData = (array) $other;
            if ($pcId !== null && isset($otherData['id']) && $otherData['id'] === $pcId) {
                continue;
            }
            $otherPcs[] = $otherData;
        }

        $neighborsHealthy = false;
        if (! empty($otherPcs)) {
            $connectedCount = 0;
            foreach ($otherPcs as $other) {
                if (isset($other['status']) && $other['status'] === 'connected') {
                    $connectedCount++;

                    continue;
                }
                $otherHb = self::toTimestamp($other['last_heartbeat_at'] ?? $other['last_seen_at'] ?? null);
                $otherThreshold = self::resolveTimeout($other, $timeoutS);
                if ($otherHb !== null && ($nowTimestamp - $otherHb) <= $otherThreshold) {
                    $connectedCount++;
                }
            }

            $ratio = $connectedCount / count($otherPcs);
            $neighborsHealthy = ($ratio >= 0.80);
            $reasons[] = "{$connectedCount} of ".count($otherPcs).' other PCs in the lab are connected ('.round($ratio * 100).'%, '
                .($neighborsHealthy ? 'at least' : 'below').' 80%)';
        } else {
            $reasons[] = 'no other PCs in the lab to compare with';
        }

        // Condition 3: The PC did NOT report a sleep or shutdown event
        $reportedSleepOrShutdown = ! empty($pcData['sleep_or_shutdown']) ||
                                   ! empty($pcData['reported_sleep_shutdown']) ||
                                   ! empty($pcData['sleep_reported']) ||
                                   ! empty($pcData['shutdown_reported']);
        $reasons[] = $reportedSleepOrShutdown ? 'the PC reported sleep or shutdown' : 'no sleep or shutdown was reported';

        // Condition 4: No reconnect has happened
        $reconnected = ! empty($pcData['reconnected']);
        $reasons[] = $reconnected ? 'the PC has reconnected' : 'no reconnect since';

        // Escalate to SUSPECTED_TAMPERING only if ALL conditions are true
        if ($longerThan5Min && $neighborsHealthy && ! $reportedSleepOrShutdown && ! $reconnected) {
            return ['status' => self::STATUS_TAMPERING, 'reasons' => $reasons];
        }

        return ['status' => self::STATUS_UNAVAILABLE, 'reasons' => $reasons];
    }

    private static function resolveTimeout(array $pcData, ?int $override = null): int
    {
        if ($override !== null && $override > 0) {
            return $override;
        }
        $customHb = (int) ($pcData['heartbeat_s'] ?? 0);
        if ($customHb > 0) {
            return max(10, $customHb * 3);
        }
        if (function_exists('app') && app()->bound('config')) {
            return max(10, (int) config('xephyguard.heartbeat_timeout_s', 45));
        }

        return 45;
    }

    private static function toTimestamp($val): ?int
    {
        if ($val === null) {
            return null;
        }
        if ($val instanceof DateTimeInterface) {
            return $val->getTimestamp();
        }
        if (is_numeric($val)) {
            return (int) $val;
        }
        if (is_string($val)) {
            $ts = strtotime($val);

            return $ts !== false ? $ts : null;
        }

        return null;
    }
}
