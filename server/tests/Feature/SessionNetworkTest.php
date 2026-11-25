<?php

namespace Tests\Feature;

use App\Jobs\ScoreSession;
use App\Models\Alert;
use App\Models\Command;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\TimetableEntry;
use App\Services\AgentHealthClassifier;
use App\Services\PolicyService;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Support\Carbon;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class SessionNetworkTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_test_session_blocks_and_ending_relaxes(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc1] = $this->enrolledPc('192.168.10.21', 'PC-01');
        [$pc2] = $this->enrolledPc('192.168.10.22', 'PC-02');
        $sm = app(SessionManager::class);
        $pb = app(PolicyService::class);
        $alwaysVersion = $pb->forPc($pc1)['version'];

        $session = $this->actingAs($this->user('assistant'))->post('/sessions', ['course' => 'CS101',
            'profile_id' => $this->profile('Test: AI blocked')->id])->assertRedirect();
        $session = LabSession::activeFor($lab->id);
        $state = $this->netState();
        $this->assertEqualsCanonicalizing(['192.168.10.21', '192.168.10.22'], $state['groups']['test']);
        $this->assertContains('ai', $state['group_categories']['test']);
        $this->assertContains('chatgpt.com', $state['categories']['ai']);
        $this->assertContains('dns.google', $state['categories']['doh']);
        $this->assertContains('8.8.8.8', $state['doh_ips']);
        $this->assertTrue($state['google']['force_udm14']);
        $policy = $pb->forPc($pc1);
        $this->assertSame('test', $policy['mode']);
        $this->assertNotSame($alwaysVersion, $policy['version']);
        $this->assertSame(2, Command::where('type', 'policy_updated')->count());

        // Accommodation: PC-02 gets a non-blocking profile -> exempt group.
        $sm->setOverride($session, $pc2, $this->profile('Practice: AI allowed, logged'), 'approved accommodation');
        $state = $this->netState();
        $this->assertSame(['192.168.10.22'], $state['groups']['exempt']);
        $this->assertSame(['192.168.10.21'], $state['groups']['test']);
        $this->assertDatabaseHas('audit_log', ['action' => 'session.override']);

        // Isolation.
        $sm->isolate($pc1, true);
        $this->assertSame(['192.168.10.21'], $this->netState()['groups']['isolated']);
        $sm->isolate($pc1, false);

        $sm->stop($session);
        $state = $this->netState();
        $this->assertSame([], $state['groups']['test']);
        $this->assertSame('always_on', $pb->forPc($pc1)['mode']);
        Queue::assertPushed(ScoreSession::class, fn ($j) => $j->sessionId === $session->id);
    }

    public function test_ending_a_session_survives_an_unreachable_analysis_service(): void
    {
        $lab = $this->bootLab();      // sync queue, analysis.test does not resolve
        Http::fake(fn () => throw new ConnectionException('down'));
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        app(SessionManager::class)->stop($s);
        $this->assertSame('ended', $s->fresh()->status);
        $this->assertSame([], $this->netState()['groups']['test']);
        $this->assertDatabaseHas('reports', ['lab_session_id' => $s->id, 'status' => 'failed']);
    }

    public function test_practice_block_profile_uses_block_ai_group(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $this->enrolledPc('192.168.10.30', 'PC-30');
        app(SessionManager::class)->start($lab, $this->profile('Practice: AI blocked'), 'CS102');
        $state = $this->netState();
        $this->assertSame(['192.168.10.30'], $state['groups']['block_ai']);
        $this->assertSame(['ai'], $state['group_categories']['block_ai']);
    }

    public function test_timetable_starts_and_stops_sessions(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $this->enrolledPc();
        Carbon::setTestNow(Carbon::parse('2026-11-02 09:30:00'));   // a Monday
        TimetableEntry::create(['lab_id' => $lab->id, 'profile_id' => $this->profile('Test: AI blocked')->id, 'course' => 'CS201',
            'weekday' => 1, 'start_time' => '09:00:00', 'end_time' => '10:00:00', 'enabled' => true]);
        $sm = app(SessionManager::class);
        $this->assertSame([1, 0], $sm->timetableTick());
        $this->assertSame('CS201', LabSession::activeFor($lab->id)->course);
        $this->assertSame([0, 0], $sm->timetableTick());
        Carbon::setTestNow(Carbon::parse('2026-11-02 10:00:30'));
        $this->assertSame([0, 1], $sm->timetableTick());
        $this->assertNull(LabSession::activeFor($lab->id));
        Carbon::setTestNow();
    }

    public function test_machine_silent_detection(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        [$pc2] = $this->enrolledPc('192.168.10.22', 'PC-02');
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        // One missed 15s beat (30s ago) must stay online under 45s default timeout
        $pc->update(['last_seen_at' => now()->subSeconds(30), 'status' => 'clean']);
        $this->assertTrue($pc->fresh()->online());
        $this->assertSame(0, app(SessionManager::class)->detectSilent());
        // Beyond 45s triggers machine_silent once
        $pc->update(['last_seen_at' => now()->subSeconds(50), 'status' => 'clean']);
        $pc2->update(['last_seen_at' => now()->subSeconds(50), 'status' => 'clean', 'clean_shutdown' => true]);
        $this->assertSame(1, app(SessionManager::class)->detectSilent());
        $alert = Alert::sole();
        $this->assertSame('tamper.machine_silent', $alert->rule_id);
        $this->assertSame('high', $alert->severity);
        $this->assertSame(0, app(SessionManager::class)->detectSilent());   // alerted once
    }

    public function test_machine_silent_alert_carries_health_label_and_reasons(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        [$pc2] = $this->enrolledPc('192.168.10.22', 'PC-02');
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $pc2->update(['last_seen_at' => now()->subSeconds(5), 'status' => 'clean']);

        // Just over the timeout: unavailable, with the numbers that led to the label.
        $pc->update(['last_seen_at' => now()->subSeconds(50), 'status' => 'clean']);
        $this->assertSame(1, app(SessionManager::class)->detectSilent());
        $details = Alert::sole()->details;
        $this->assertSame(AgentHealthClassifier::STATUS_UNAVAILABLE, $details['health']);
        $this->assertContains('silent for 5 minutes or less', $details['reasons']);
        $this->assertContains('1 of 1 other PCs in the lab are connected (100%, at least 80%)', $details['reasons']);

        // Silent for over 5 minutes while the neighbour is fine: suspected tampering, reasons say why.
        Alert::query()->delete();
        $pc->fresh()->update(['last_seen_at' => now()->subSeconds(400), 'status' => 'clean', 'silent_alerted_at' => null]);
        $this->assertSame(1, app(SessionManager::class)->detectSilent());
        $details = Alert::sole()->details;
        $this->assertSame(AgentHealthClassifier::STATUS_TAMPERING, $details['health']);
        $this->assertContains('silent for more than 5 minutes', $details['reasons']);
        $this->assertContains('no sleep or shutdown was reported', $details['reasons']);
    }

    public function test_proxy_net_events_need_internal_token(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $payload = ['events' => [['client_ip' => '192.168.10.21', 'host' => 'chatgpt.com', 'layer' => 'sni', 'action' => 'blocked',
            'category' => 'ai', 'ts' => now()->getTimestampMs()]]];
        $this->postJson('/api/internal/net-events', $payload)->assertUnauthorized();
        $this->withToken('test-internal-token')->postJson('/api/internal/net-events', $payload)->assertOk()->assertJson(['stored' => 1]);
        $this->assertDatabaseHas('net_events', ['pc_id' => $pc->id, 'host' => 'chatgpt.com', 'action' => 'blocked']);
        $this->flushHeaders()->getJson('/api/internal/eval/results?pc_ip=192.168.10.21&since_ms=0')->assertUnauthorized();
        $this->withToken('test-internal-token')->getJson('/api/internal/eval/results?pc_ip=192.168.10.21&since_ms='.(now()->getTimestampMs() - 60000))
            ->assertOk()->assertJsonPath('net_events.0.host', 'chatgpt.com')->assertJsonPath('pc.name', 'PC-01');
    }

    public function test_observed_ai_and_ide_net_events_raise_alert_and_surface_in_dashboard(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $pc->update(['last_seen_at' => now(), 'status' => 'clean']);
        app(SessionManager::class)->start($lab, $this->profile('Practice: AI allowed, logged'), 'CS101');
        $ts = now()->getTimestampMs();

        $payload = ['events' => [
            ['client_ip' => '::ffff:192.168.10.21', 'host' => 'api.individual.githubcopilot.com', 'layer' => 'sni',
                'action' => 'observed', 'category' => 'ide', 'matched' => 'githubcopilot.com', 'ts' => $ts],
            ['client_ip' => '192.168.10.21', 'host' => 'api.individual.githubcopilot.com', 'layer' => 'sni',
                'action' => 'observed', 'category' => 'ide', 'matched' => 'githubcopilot.com', 'ts' => $ts + 1000],
        ]];

        $this->withToken('test-internal-token')->postJson('/api/internal/net-events', $payload)
            ->assertOk()->assertJson(['stored' => 2]);

        $this->assertDatabaseHas('net_events', [
            'pc_id' => $pc->id, 'client_ip' => '192.168.10.21',
            'host' => 'api.individual.githubcopilot.com', 'action' => 'observed', 'category' => 'ide',
        ]);
        // Two matching events in window deduplicate into a single high-severity alert
        $this->assertSame(1, Alert::where('pc_id', $pc->id)->where('rule_id', 'proxy.ide_ai_observed')->count());
        $this->assertSame('alert', $pc->fresh()->status);

        $this->actingAs($this->user('assistant'))
            ->getJson("/pcs/{$pc->id}")
            ->assertOk()
            ->assertJsonPath('ai_traffic.0.domain', 'api.individual.githubcopilot.com')
            ->assertJsonPath('ai_traffic.0.mode', 'Observed')
            ->assertJsonPath('ai_traffic.0.count', 2)
            ->assertJsonPath('network.0.action', 'observed');
    }

    public function test_report_show_and_pdf_render_when_blocked_net_events_exist(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $session = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        NetEvent::create([
            'pc_id' => $pc->id,
            'lab_session_id' => $session->id,
            'client_ip' => '192.168.10.21',
            'host' => 'chatgpt.com',
            'layer' => 'sni',
            'action' => 'blocked',
            'category' => 'ai',
            'matched' => 'chatgpt.com',
            'occurred_at' => now(),
        ]);
        app(SessionManager::class)->stop($session);

        $this->actingAs($this->user('instructor'))
            ->get("/reports/{$session->id}")
            ->assertOk()
            ->assertSee('chatgpt.com')
            ->assertSee('PC-01');

        $pdf = $this->actingAs($this->user('instructor'))
            ->get("/reports/{$session->id}/pdf")
            ->assertOk();
        $this->assertStringStartsWith('%PDF', $pdf->getContent());
    }
}
