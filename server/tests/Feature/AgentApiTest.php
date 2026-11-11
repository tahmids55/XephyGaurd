<?php

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Command;
use App\Models\Event;
use App\Models\KeyStat;
use App\Models\PasteEvent;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class AgentApiTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function batch(int $pcId, array $events, array $hb = []): array
    {
        return ['v' => 1, 'pc_id' => (string) $pcId, 'heartbeat' => $hb + ['mode' => 'test', 'user' => 'alice',
            'agent_version' => '0.1.0', 'local_time_ms' => now()->getTimestampMs() - 2000, 'capabilities' => ['keys' => true]],
            'events' => $events];
    }

    private function ev(int $seq, string $type, array $data): array
    {
        return ['seq' => $seq, 'ts' => now()->getTimestampMs() - 2000, 'type' => $type, 'data' => $data];
    }

    public function test_mtls_and_token_are_both_required(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        [, , $otherCert] = $this->enrolledPc('192.168.10.22', 'PC-02');

        $this->getJson('/api/agent/v1/policy')->assertUnauthorized();
        $this->withHeaders(['Authorization' => "Bearer $token"])->getJson('/api/agent/v1/policy')->assertForbidden();
        $this->withHeaders($this->agentHeaders($token, $otherCert))->getJson('/api/agent/v1/policy')->assertForbidden();
        $this->withHeaders($this->agentHeaders($token, $cert))->getJson('/api/agent/v1/policy')->assertOk()
            ->assertJsonStructure(['policy' => ['version', 'mode'], 'signatures' => ['version', 'bundle', 'signature'], 'realtime']);
    }

    public function test_batch_ingest_alerts_dedup_and_ack(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $h = $this->agentHeaders($token, $cert);
        $events = [
            $this->ev(1, 'rule_hit', ['rule_id' => 'proc.ollama', 'severity' => 'high', 'category' => 'local_llm',
                'description' => 'Ollama', 'subject' => 'ollama', 'actions' => ['log', 'warn', 'alert'], 'user' => 'alice']),
            $this->ev(2, 'key_stats', ['window_start' => now()->getTimestampMs() - 7000, 'window_s' => 5, 'keys' => 40,
                'backspaces' => 3, 'iki_mean' => 120.5, 'bursts' => 2, 'paste_chords' => 1, 'user' => 'alice']),
            $this->ev(3, 'paste', ['chars' => 1500, 'lines' => 60, 'code' => true, 'score' => 0.9, 'src' => 'firefox', 'dst' => 'code', 'user' => 'alice']),
            $this->ev(4, 'tamper', ['kind' => 'clock_change', 'delta_ms' => -3600000]),
            $this->ev(5, 'window_focus', ['app' => 'code', 'title' => 'code', 'user' => 'alice']),
        ];
        $r = $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $events))->assertOk();
        $this->assertSame(5, $r->json('ack_seq'));
        $this->assertSame(5, Event::count());
        $this->assertSame(1, KeyStat::count());
        $this->assertSame(1, PasteEvent::count());
        $this->assertEqualsCanonicalizing(['proc.ollama', 'behavior.large_paste', 'tamper.clock_change'], Alert::pluck('rule_id')->all());
        $pc->refresh();
        $this->assertSame('alert', $pc->status);
        $this->assertSame('alice', $pc->current_user);
        $this->assertSame('code', $pc->active_app);
        $this->assertGreaterThan(0, $pc->live_score);
        $this->assertNotEquals(0, $pc->agent->clock_offset_ms);

        // Re-sent batch after a lost ack: nothing duplicated.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $events))->assertOk()->assertJson(['ack_seq' => 5]);
        $this->assertSame(5, Event::count());
        $this->assertSame(3, Alert::count());

        // Agent restarted (its local de-dup is reset) and reports the same hit with new seq numbers.
        $again = [
            $this->ev(6, 'rule_hit', $events[0]['data']),
            $this->ev(7, 'process_snapshot', ['hash' => 'h1', 'count' => 1, 'list' => [['pid' => 10, 'exe' => 'code', 'user' => 'alice', 'rss_mb' => 120, 'rule_id' => '']]]),
            $this->ev(8, 'process_snapshot', ['hash' => 'h2', 'count' => 2, 'list' => [['pid' => 42, 'exe' => 'ollama', 'user' => 'alice', 'rss_mb' => 800, 'rule_id' => 'proc.ollama'], ['pid' => 10, 'exe' => 'code', 'user' => 'alice', 'rss_mb' => 125, 'rule_id' => '']]]),
        ];
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $again))->assertOk();
        $this->assertSame(3, Alert::count());
        // Only the latest process_snapshot row is kept per PC (zero DB bloat)
        $this->assertSame(1, Event::where('pc_id', $pc->id)->where('type', 'process_snapshot')->count());
        $this->actingAs($this->user('instructor'))
            ->getJson("/pcs/{$pc->id}")
            ->assertOk()
            ->assertJsonPath('running_processes.0.exe', 'ollama')
            ->assertJsonPath('running_processes.1.exe', 'code');
    }

    public function test_rejects_malformed_and_foreign_batches(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id + 99, []))->assertUnprocessable();
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [['seq' => 'x', 'type' => 'rule_hit']]))->assertUnprocessable();
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [$this->ev(1, 'rm_rf', [])]))->assertUnprocessable();
        $this->withHeaders($h)->postJson('/api/agent/v1/events', ['v' => 2] + $this->batch($pc->id, []))->assertUnprocessable();
    }

    public function test_unclean_restart_and_command_results(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $cmd = Command::create(['pc_id' => $pc->id, 'type' => 'ping']);
        $this->withHeaders($this->agentHeaders($token, $cert))->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'agent_started', ['previous_exit' => 'unclean']),
            $this->ev(2, 'command_result', ['command_id' => (string) $cmd->id, 'status' => 'ok', 'detail' => ['pong' => 1]]),
        ]))->assertOk();
        $this->assertSame('tamper.unclean_restart', Alert::first()->rule_id);
        $this->assertSame('ok', $cmd->fresh()->status);
    }

    public function test_broadcast_auth_only_for_own_channel(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);
        $ok = $this->withHeaders($h)->postJson('/api/agent/v1/broadcasting/auth', ['socket_id' => '123.456', 'channel_name' => "private-pc.{$pc->id}"])
            ->assertOk()->json('auth');
        $key = config('reverb.apps.apps.0.key');
        $this->assertSame("$key:".hash_hmac('sha256', "123.456:private-pc.{$pc->id}", config('reverb.apps.apps.0.secret')), $ok);
        $this->withHeaders($h)->postJson('/api/agent/v1/broadcasting/auth', ['socket_id' => '123.456', 'channel_name' => 'private-pc.999'])->assertForbidden();
        $this->withHeaders($h)->postJson('/api/agent/v1/broadcasting/auth', ['socket_id' => '123.456', 'channel_name' => 'private-lab.1'])->assertForbidden();
    }

    public function test_revoked_and_retired_agents_are_refused(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $pc->update(['retired_at' => now()]);
        $this->withHeaders($this->agentHeaders($token, $cert))->getJson('/api/agent/v1/policy')->assertUnauthorized();
    }

    public function test_copy_from_chrome_and_paste_in_vscode_updates_dashboard(): void
    {
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);

        // 1. Policy includes clipboard and keys collection
        $pol = $this->withHeaders($h)->getJson('/api/agent/v1/policy')->assertOk()->json('policy');
        $this->assertTrue($pol['collect']['clipboard']);
        $this->assertTrue($pol['collect']['keys']);

        // 2. Student copies in Chrome
        $events = [
            $this->ev(1, 'window_focus', ['app' => 'chrome', 'title' => 'Tutorial - Google Chrome', 'user' => 'student1']),
            $this->ev(2, 'copy', ['chars' => 200, 'lines' => 8, 'code' => true, 'score' => 0.95, 'src' => 'chrome', 'dst' => '', 'user' => 'student1']),
        ];
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $events))->assertOk();

        $pc->refresh();
        $this->assertStringContainsString('Copy: chrome', $pc->last_event);

        // 3. Student switches to VS Code and pastes
        $events2 = [
            $this->ev(3, 'window_focus', ['app' => 'code', 'title' => 'main.cpp - Visual Studio Code', 'user' => 'student1']),
            $this->ev(4, 'paste', ['chars' => 200, 'lines' => 8, 'code' => true, 'score' => 0.95, 'src' => 'chrome', 'dst' => 'code', 'user' => 'student1']),
        ];
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $events2))->assertOk();

        $pc->refresh();
        // Tile reflects chrome -> code paste
        $this->assertStringContainsString('Paste: chrome → code', $pc->last_event);

        // Teacher dashboard PC drawer shows paste in timeline and pastes tab
        $dash = $this->actingAs($this->user('instructor'))->getJson("/pcs/{$pc->id}")->assertOk()->json();
        $this->assertSame('Paste: chrome → code (200 ch, 8 ln)', $dash['pc']['last_event']);

        $timelineSummary = collect($dash['timeline'])->firstWhere('type', 'paste')['summary'];
        $this->assertStringContainsString('chrome → code: paste 200 chars / 8 lines (code)', $timelineSummary);

        $pastesTab = collect($dash['pastes'])->firstWhere('is_copy', false);
        $this->assertSame('chrome', $pastesTab['source_app']);
        $this->assertSame('code', $pastesTab['target_app']);
        $this->assertSame(200, $pastesTab['chars']);

        // 4. Student copies in a.cpp and pastes in b.cpp in the same VS Code
        $events3 = [
            $this->ev(5, 'window_focus', ['app' => 'code', 'title' => 'a.cpp - Project - Visual Studio Code', 'user' => 'student1']),
            $this->ev(6, 'copy', ['chars' => 85, 'lines' => 4, 'code' => true, 'score' => 0.9, 'src' => 'code (a.cpp)', 'dst' => '', 'user' => 'student1']),
            $this->ev(7, 'window_focus', ['app' => 'code', 'title' => 'b.cpp - Project - Visual Studio Code', 'user' => 'student1']),
            $this->ev(8, 'paste', ['chars' => 85, 'lines' => 4, 'code' => true, 'score' => 0.9, 'src' => 'code (a.cpp)', 'dst' => 'code (b.cpp)', 'user' => 'student1']),
        ];
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, $events3))->assertOk();

        $pc->refresh();
        $this->assertSame('Paste: code (a.cpp) → code (b.cpp) (85 ch, 4 ln)', $pc->last_event);

        $dash2 = $this->actingAs($this->user('instructor'))->getJson("/pcs/{$pc->id}")->assertOk()->json();
        $this->assertSame('Paste: code (a.cpp) → code (b.cpp) (85 ch, 4 ln)', $dash2['pc']['last_event']);

        $timelineSummary2 = collect($dash2['timeline'])->firstWhere('summary', 'code (a.cpp) → code (b.cpp): paste 85 chars / 4 lines (code)');
        $this->assertNotNull($timelineSummary2);

        $pastesTab2 = collect($dash2['pastes'])->where('is_copy', false)->firstWhere('source_app', 'code (a.cpp)');
        $this->assertSame('code (b.cpp)', $pastesTab2['target_app']);
        $this->assertSame(85, $pastesTab2['chars']);
    }

    public function test_policy_includes_browser_ai_overview_suppression(): void
    {
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);

        // 1. Session with 'Test: AI blocked' (google_udm14 is true)
        $profile = $this->profile('Test: AI blocked');
        app(SessionManager::class)->start($lab, $profile, 'EXAM1');

        $res = $this->withHeaders($h)->getJson('/api/agent/v1/policy')->assertOk()->json();
        $this->assertTrue($res['policy']['suppress_ai_overview']);

        // 2. Update profile to disable google_udm14
        $net = $profile->network;
        $net['google_udm14'] = false;
        $profile->update(['network' => $net]);

        $res2 = $this->withHeaders($h)->getJson('/api/agent/v1/policy')->assertOk()->json();
        $this->assertFalse($res2['policy']['suppress_ai_overview']);
    }
}
