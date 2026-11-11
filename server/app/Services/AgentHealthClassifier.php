<?php

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
     * @param array<string, mixed>|object $pc
     * @param array<int, array<string, mixed>|object> $labSnapshot
     * @param DateTimeInterface|int|null $now
     * @return string 'OK' | 'AGENT_UNAVAILABLE' | 'SUSPECTED_TAMPERING'
     */
    public static function classify($pc, array $labSnapshot = [], $now = null): string
    {
        $nowTimestamp = self::toTimestamp($now) ?? time();
        $pcData = (array) $pc;

        $lastHeartbeat = self::toTimestamp($pcData['last_heartbeat_at'] ?? $pcData['last_seen_at'] ?? null);

        // If never seen or no heartbeat recorded, it's unavailable
        if ($lastHeartbeat === null) {
            $elapsed = PHP_INT_MAX;
        } else {
            $elapsed = max(0, $nowTimestamp - $lastHeartbeat);
        }

        // Heartbeat interval is 10s. Missing 3 heartbeats (> 30s) marks agent unavailable.
        if ($elapsed <= 30) {
            return self::STATUS_OK;
        }

        // Condition 1: Unavailable longer than 5 minutes (> 300s)
        $longerThan5Min = $elapsed > 300;

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
        if (!empty($otherPcs)) {
            $connectedCount = 0;
            foreach ($otherPcs as $other) {
                if (isset($other['status']) && $other['status'] === 'connected') {
                    $connectedCount++;
                    continue;
                }
                $otherHb = self::toTimestamp($other['last_heartbeat_at'] ?? $other['last_seen_at'] ?? null);
                if ($otherHb !== null && ($nowTimestamp - $otherHb) <= 30) {
                    $connectedCount++;
                }
            }

            $ratio = $connectedCount / count($otherPcs);
            $neighborsHealthy = ($ratio >= 0.80);
        }

        // Condition 3: The PC did NOT report a sleep or shutdown event
        $reportedSleepOrShutdown = !empty($pcData['sleep_or_shutdown']) ||
                                   !empty($pcData['reported_sleep_shutdown']) ||
                                   !empty($pcData['sleep_reported']) ||
                                   !empty($pcData['shutdown_reported']);

        // Condition 4: No reconnect has happened
        $reconnected = !empty($pcData['reconnected']);

        // Escalate to SUSPECTED_TAMPERING only if ALL conditions are true
        if ($longerThan5Min && $neighborsHealthy && !$reportedSleepOrShutdown && !$reconnected) {
            return self::STATUS_TAMPERING;
        }

        return self::STATUS_UNAVAILABLE;
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
