<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Event;
use App\Services\InspectorQueries;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-44 (I-SERVER-34): browser tabs only from the session window; the inspector GET never writes. */
class InspectorSessionWindowTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_browser_tabs_only_from_the_active_session(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $focus = fn (int $seq, $at, string $title) => Event::create(['pc_id' => $pc->id, 'agent_id' => $pc->agent->id,
            'received_at' => now(), 'seq' => $seq, 'type' => 'window_focus', 'occurred_at' => $at,
            'data' => ['app' => 'firefox', 'title' => $title]]);
        $focus(1, now()->subHours(2), 'earlier class');
        $session = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $focus(2, now()->addSecond(), 'this class');

        $d = app(InspectorQueries::class)->forPc($pc, $session, false);
        $this->assertSame(['this class'], $d['browser_tabs']->pluck('title')->all());
        // Without a session: last 24 h.
        $focus(3, now()->subHours(30), 'yesterday');
        $this->assertSame(['this class', 'earlier class'], app(InspectorQueries::class)->forPc($pc, null, false)['browser_tabs']->pluck('title')->all());
    }

    public function test_inspector_get_does_not_write(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['status' => 'clean', 'live_score' => 100, 'last_seen_at' => now()]);
        $admin = $this->user('admin');
        $writes = [];
        DB::listen(function ($q) use (&$writes) {
            if (preg_match('/^\s*(update|insert|delete)\b/i', $q->sql) && ! str_contains($q->sql, '"sessions"')) {
                $writes[] = $q->sql;
            }
        });

        $this->actingAs($admin)->getJson('/pcs/'.$pc->id)->assertOk();

        $this->assertSame([], $writes);
    }

    public function test_session_start_stop_and_silent_alert_refresh_the_live_score(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['status' => 'alert', 'live_score' => 30, 'last_seen_at' => now()]);
        Alert::create(['pc_id' => $pc->id, 'lab_session_id' => null, 'rule_id' => 'x', 'severity' => 'high',
            'category' => 'tamper', 'title' => 'before the session', 'occurred_at' => now()]);
        $sessions = app(SessionManager::class);

        $session = $sessions->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $this->assertSame(0, $pc->fresh()->live_score);   // only this session's alerts count
        $this->assertSame('clean', $pc->fresh()->status);

        $pc->update(['last_seen_at' => now()->subMinutes(5)]);
        $this->assertSame(1, $sessions->detectSilent());
        $this->assertSame(30, $pc->fresh()->live_score);  // the session's silent alert (high)
        $this->assertSame('offline', $pc->fresh()->status);

        $sessions->stop($session);
        $this->assertSame(60, $pc->fresh()->live_score);  // both open alerts again
    }
}
