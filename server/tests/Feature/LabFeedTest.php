<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Event;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** Live lab feed (/lab/feed): tiles, alerts and a lab-wide timeline for every PC. */
class LabFeedTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_feed_returns_tiles_alerts_and_events_for_all_pcs(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['status' => 'alert', 'live_score' => 30, 'last_seen_at' => now()]);
        $ev = fn (int $seq, string $type, array $data) => Event::create(['pc_id' => $pc->id, 'agent_id' => $pc->agent->id,
            'received_at' => now(), 'seq' => $seq, 'type' => $type, 'occurred_at' => now(), 'data' => $data]);
        $ev(1, 'process_start', ['exe' => 'ollama']);
        $ev(2, 'heartbeat', []);
        $ev(3, 'window_focus', ['app' => 'firefox', 'title' => 'private title']);
        Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r1', 'severity' => 'high',
            'category' => 'tamper', 'title' => 'agent stopped', 'occurred_at' => now()]);

        $res = $this->actingAs($this->user('assistant'))->getJson('/lab/feed')->assertOk();

        $this->assertSame([$pc->id], array_column($res->json('tiles'), 'id'));
        $this->assertSame(30, $res->json('tiles.0.score'));
        $this->assertSame(['agent stopped'], array_column($res->json('alerts'), 'title'));
        // Heartbeats and window titles stay out of the lab-wide feed.
        $this->assertSame(['started ollama'], array_column($res->json('events'), 'summary'));
        $this->assertSame($pc->name, $res->json('events.0.pc'));
    }

    public function test_feed_needs_a_signed_in_user(): void
    {
        $this->bootLab();
        $this->getJson('/lab/feed')->assertUnauthorized();
    }
}
