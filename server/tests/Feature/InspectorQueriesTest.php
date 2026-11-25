<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Event;
use App\Services\InspectorQueries;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-43: the inspector (PC detail) data comes from InspectorQueries (moved out of DashboardController). */
class InspectorQueriesTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_for_pc_returns_the_drawer_data_and_hides_evidence_without_permission(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->processSnapshot($pc, [['pid' => 42, 'exe' => 'python3']]);
        Event::create(['pc_id' => $pc->id, 'agent_id' => $pc->agent->id, 'received_at' => now(), 'seq' => 2, 'type' => 'process_start', 'occurred_at' => now(),
            'data' => ['pid' => 42, 'exe' => 'python3']]);
        Event::create(['pc_id' => $pc->id, 'agent_id' => $pc->agent->id, 'received_at' => now(), 'seq' => 3, 'type' => 'process_start', 'occurred_at' => now()->subHours(30),
            'data' => ['pid' => 7, 'exe' => 'old']]);

        $d = app(InspectorQueries::class)->forPc($pc, null, false);

        foreach (['pc', 'student', 'alerts', 'timeline', 'running_processes', 'extensions', 'artifacts', 'browser_tabs',
            'ai_traffic', 'processes', 'adapters', 'listening_ports', 'devices', 'network', 'pastes', 'keyrate', 'evidence'] as $key) {
            $this->assertArrayHasKey($key, $d);
        }
        $this->assertSame([], $d['evidence']);
        $this->assertSame([['pid' => 42, 'exe' => 'python3']], $d['running_processes']);
        // Without a session the window is the last 24 h: the 30 h old start is not listed.
        $this->assertSame(['python3'], $d['processes']->pluck('exe')->all());
        $this->assertSame('started python3', InspectorQueries::summarize('process_start', ['exe' => 'python3']));
    }
}
