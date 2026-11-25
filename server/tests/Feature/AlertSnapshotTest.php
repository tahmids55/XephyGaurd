<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Agent;
use App\Models\Alert;
use App\Models\AlertSnapshot;
use App\Models\Event;
use App\Models\NetEvent;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F123: every alert freezes the context the CCC already holds (DECISIONS #32). */
class AlertSnapshotTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function event($pc, int $seq, string $type, array $data, $at): void
    {
        Event::create(['pc_id' => $pc->id, 'agent_id' => Agent::where('pc_id', $pc->id)->value('id'), 'seq' => $seq,
            'type' => $type, 'occurred_at' => $at, 'received_at' => now(), 'data' => $data]);
    }

    public function test_new_alert_freezes_processes_recent_events_and_net_with_a_hash(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['current_user' => 'alice', 'active_app' => 'firefox']);
        $this->processSnapshot($pc, [['pid' => 42, 'exe' => 'ollama', 'name' => 'ollama']], 1);
        $this->event($pc, 2, 'window_focus', ['app' => 'firefox', 'title' => 'ChatGPT'], now()->subSeconds(10));
        $this->event($pc, 3, 'heartbeat', ['uptime_s' => 5], now()->subSeconds(5));
        $this->event($pc, 4, 'window_focus', ['app' => 'old', 'title' => 'old'], now()->subMinutes(10));
        NetEvent::create(['pc_id' => $pc->id, 'client_ip' => $pc->ip, 'host' => 'chatgpt.com', 'layer' => 'dns', 'action' => 'blocked',
            'category' => 'ai', 'matched' => 'chatgpt.com', 'occurred_at' => now()->subSeconds(3)]);

        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'ai.local', 'severity' => 'high', 'title' => 'Ollama',
            'details' => ['why' => 'process'], 'occurred_at' => now()]);

        $snap = AlertSnapshot::where('alert_id', $alert->id)->sole();
        $this->assertTrue($snap->intact());
        $this->assertSame($snap->id, $alert->snapshot->id);
        $d = $snap->data();
        $this->assertSame('ai.local', $d['alert']['rule_id']);
        $this->assertSame(['why' => 'process'], $d['alert']['details']);
        $this->assertSame('alice', $d['pc']['user']);
        $this->assertSame('ollama', $d['processes']['list'][0]['exe']);
        $this->assertSame(['window_focus'], array_column($d['events'], 'type'));
        $this->assertSame('ChatGPT', $d['events'][0]['data']['title']);
        $this->assertSame('chatgpt.com', $d['net'][0]['host']);
    }

    public function test_edited_payload_no_longer_matches_its_hash(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'low', 'title' => 'r', 'occurred_at' => now()]);
        $snap = $alert->snapshot;
        $this->assertNull($snap->data()['processes']);
        $snap->update(['payload' => str_replace('"r"', '"x"', $snap->payload)]);
        $this->assertFalse($snap->fresh()->intact());
    }
}
