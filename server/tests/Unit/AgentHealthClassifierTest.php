<?php

namespace Tests\Unit;

use App\Services\AgentHealthClassifier;
use PHPUnit\Framework\TestCase;

class AgentHealthClassifierTest extends TestCase
{
    private int $now = 1700000000;

    public function test_recent_heartbeat_is_ok(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 10,
        ];

        $status = AgentHealthClassifier::classify($pc, [], $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_OK, $status);
    }

    public function test_missed_three_heartbeats_is_agent_unavailable(): void
    {
        // 35s ago (> 30s, missed 3 10s heartbeats, but < 5 min)
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 35,
        ];

        $status = AgentHealthClassifier::classify($pc, [], $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_whole_lab_outage_stays_unavailable(): void
    {
        // Target PC down for 10 minutes (600s)
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 600,
            'reported_sleep_shutdown' => false,
            'reconnected' => false,
        ];

        // All 9 neighbors in the lab are also disconnected (>30s)
        $labSnapshot = [];
        for ($i = 2; $i <= 10; $i++) {
            $labSnapshot[] = [
                'id' => $i,
                'last_heartbeat_at' => $this->now - 600,
            ];
        }

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_neighbor_ratio_below_80_percent_stays_unavailable(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 600,
            'reported_sleep_shutdown' => false,
            'reconnected' => false,
        ];

        // 10 other PCs: 7 connected (70%), 3 disconnected
        $labSnapshot = [];
        for ($i = 2; $i <= 8; $i++) {
            $labSnapshot[] = ['id' => $i, 'last_heartbeat_at' => $this->now - 5];
        }
        for ($i = 9; $i <= 11; $i++) {
            $labSnapshot[] = ['id' => $i, 'last_heartbeat_at' => $this->now - 600];
        }

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_single_pc_drop_with_healthy_neighbors_escalates_to_suspected_tampering(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 350, // > 5 minutes
            'reported_sleep_shutdown' => false,
            'reconnected' => false,
        ];

        // 10 other PCs: 9 connected (90% >= 80%)
        $labSnapshot = [];
        for ($i = 2; $i <= 10; $i++) {
            $labSnapshot[] = ['id' => $i, 'last_heartbeat_at' => $this->now - 5];
        }
        $labSnapshot[] = ['id' => 11, 'last_heartbeat_at' => $this->now - 600];

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_TAMPERING, $status);
    }

    public function test_sleep_or_shutdown_prevents_escalation(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 400,
            'reported_sleep_shutdown' => true,
            'reconnected' => false,
        ];

        // 100% neighbors healthy
        $labSnapshot = [
            ['id' => 2, 'last_heartbeat_at' => $this->now - 5],
            ['id' => 3, 'last_heartbeat_at' => $this->now - 5],
        ];

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_reconnected_prevents_escalation(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 400,
            'reported_sleep_shutdown' => false,
            'reconnected' => true,
        ];

        $labSnapshot = [
            ['id' => 2, 'last_heartbeat_at' => $this->now - 5],
        ];

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_isolated_lab_with_no_neighbors_stays_unavailable(): void
    {
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 400,
            'reported_sleep_shutdown' => false,
            'reconnected' => false,
        ];

        // Only this PC in the lab
        $labSnapshot = [$pc];

        $status = AgentHealthClassifier::classify($pc, $labSnapshot, $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }
}
