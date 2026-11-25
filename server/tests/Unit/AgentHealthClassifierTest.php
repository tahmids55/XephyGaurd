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

    public function test_single_missed_heartbeat_remains_ok(): void
    {
        // With 15s heartbeat / 45s timeout, missing 1-2 beats stays OK
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 35,
        ];

        $status = AgentHealthClassifier::classify($pc, [], $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_OK, $status);
    }

    public function test_missed_three_heartbeats_is_agent_unavailable(): void
    {
        // 50s ago (> 45s default 3x 15s timeout, < 5 min)
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 50,
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

    public function test_custom_heartbeat_interval_adjusts_threshold(): void
    {
        // Custom 5s heartbeat => 15s threshold (3x interval)
        $pc = [
            'id' => 1,
            'last_heartbeat_at' => $this->now - 20,
            'heartbeat_s' => 5,
        ];

        $status = AgentHealthClassifier::classify($pc, [], $this->now);
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $status);
    }

    public function test_assess_returns_the_same_status_with_reasons(): void
    {
        $pc = ['id' => 1, 'last_heartbeat_at' => $this->now - 400];
        $lab = [['id' => 2, 'last_heartbeat_at' => $this->now - 5]];

        $result = AgentHealthClassifier::assess($pc, $lab, $this->now);

        $this->assertSame(AgentHealthClassifier::classify($pc, $lab, $this->now), $result['status']);
        $this->assertSame(AgentHealthClassifier::STATUS_TAMPERING, $result['status']);
        $this->assertSame('no heartbeat for 400s (limit 45s)', $result['reasons'][0]);
        $this->assertNotEmpty($result['reasons']);
    }
}
