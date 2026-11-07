<?php

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\BlocklistEntry;
use App\Models\Command;
use App\Models\Event;
use App\Models\Rule;
use App\Services\CommandBus;
use App\Services\SessionManager;
use Database\Seeders\Phase0PolicySeeder;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class Phase0RegressionTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function batch(int $pcId, array $events, array $hb = []): array
    {
        return [
            'v' => 1,
            'pc_id' => (string) $pcId,
            'heartbeat' => $hb + [
                'mode' => 'always_on',
                'user' => 'student1',
                'agent_version' => '0.1.0',
                'local_time_ms' => now()->getTimestampMs() - 1000,
                'capabilities' => ['keys' => true],
            ],
            'events' => $events,
        ];
    }

    private function ev(int $seq, string $type, array $data): array
    {
        return ['seq' => $seq, 'ts' => now()->getTimestampMs() - 1000, 'type' => $type, 'data' => $data];
    }

    public function test_r5_no_popups_in_always_on_and_inventory_deduped_24h(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);

        // 1. In Always-on, even a high-severity rule_hit or extension rule_hit never creates a popup or alert.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'rule_hit', [
                'rule_id' => 'ext.jb.misc',
                'severity' => 'high',
                'category' => 'extension',
                'description' => 'JetBrains AI plugin',
                'subject' => 'continue',
                'actions' => ['log', 'warn', 'alert'],
            ]),
            $this->ev(2, 'rule_hit', [
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'alert'],
            ]),
        ]))->assertOk();

        $this->assertSame(0, Command::where('type', 'message')->count());
        $this->assertSame(0, Alert::count());

        // 2. Start a Test session: installed-only extension rule_hit creates ONE alert (deduped for 24h/session) and ZERO popups.
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(3, 'rule_hit', [
                'rule_id' => 'ext.jb.misc',
                'severity' => 'high',
                'category' => 'extension',
                'description' => 'JetBrains AI plugin',
                'subject' => 'continue',
                'actions' => ['log', 'warn', 'alert'],
            ]),
        ]))->assertOk();

        $this->assertSame(0, Command::where('type', 'message')->count(), 'Installed extension must never trigger a popup');
        $this->assertSame(1, Alert::where('rule_id', 'ext.jb.misc')->count());

        // Reporting the same extension again 2 hours later in the same session (or within 24h) is deduplicated.
        $this->travel(2)->hours();
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(4, 'rule_hit', [
                'rule_id' => 'ext.jb.misc',
                'severity' => 'high',
                'category' => 'extension',
                'description' => 'JetBrains AI plugin',
                'subject' => 'continue',
                'actions' => ['log', 'warn', 'alert'],
            ]),
        ]))->assertOk();
        $this->assertSame(1, Alert::where('rule_id', 'ext.jb.misc')->count());
    }

    public function test_r6_no_browser_kills_on_isolate_or_outside_test_and_kills_only_in_test_with_kill_action(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);
        $sm = app(SessionManager::class);

        // 1. Isolating a PC sends isolate_notice with lease_s & reason, and NEVER sends browser kill commands.
        $sm->isolate($pc, true, null, 'suspected hotspot');
        $this->assertSame(0, Command::where('type', 'kill')->count());
        $notice = Command::where('type', 'isolate_notice')->sole();
        $this->assertTrue($notice->args['isolated']);
        $this->assertSame(600, $notice->args['lease_s']);
        $this->assertSame('suspected hotspot', $notice->args['reason']);

        // 2. While isolated, focusing a browser or starting a browser in process_snapshot never kills the browser.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'window_focus', ['app' => 'chrome', 'title' => 'ChatGPT - Google Chrome']),
            $this->ev(2, 'process_snapshot', ['added' => ['chrome', 'msedge', 'firefox']]),
        ]))->assertOk();
        $this->assertSame(0, Command::where('type', 'kill')->count());
        $sm->isolate($pc, false);

        // 3. In Practice mode, even a rule_hit claiming actions=['kill'] never sends a kill command.
        $practiceSession = $sm->start($lab, $this->profile('Practice: AI blocked'), 'CS102');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(3, 'rule_hit', [
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'kill', 'alert'],
            ]),
        ]))->assertOk();
        $this->assertSame(0, Command::where('type', 'kill')->count());
        $sm->stop($practiceSession);

        // 4. In Test: AI blocked (where profile does NOT have kill on proc.ollama), no kill command is sent.
        $testSession = $sm->start($lab, $this->profile('Test: AI blocked'), 'CS103');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(4, 'rule_hit', [
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'kill', 'alert'],
            ]),
        ]))->assertOk();
        $this->assertSame(0, Command::where('type', 'kill')->count());
        $sm->stop($testSession);

        // 5. In Exam: strict (Test mode where profile rule_actions for proc.ollama includes kill), kill IS sent.
        $examSession = $sm->start($lab, $this->profile('Exam: strict'), 'CS104');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(5, 'rule_hit', [
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'kill', 'alert'],
            ]),
        ]))->assertOk();
        $this->assertSame(1, Command::where('type', 'kill')->count());
        $this->assertSame('ollama', Command::where('type', 'kill')->first()->args['exe']);
        $sm->stop($examSession);
    }

    public function test_r7_window_title_match_is_weak_evidence_never_kills_and_honors_allowlist(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);
        $sm = app(SessionManager::class);

        // 1. Outside Test (Always-on and Practice): window title match logs event, raises NO alert, NO kill, NO popup.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'window_focus', ['app' => 'chrome', 'title' => 'Claude - Anthropic - Google Chrome']),
        ]))->assertOk();
        $this->assertSame(0, Alert::count());
        $this->assertSame(0, Command::whereIn('type', ['kill', 'message', 'isolate_notice'])->count());

        $practice = $sm->start($lab, $this->profile('Practice: AI blocked'), 'CS101');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(2, 'window_focus', ['app' => 'chrome', 'title' => 'ChatGPT - OpenAI']),
        ]))->assertOk();
        $this->assertSame(0, Alert::count());
        $this->assertSame(0, Command::whereIn('type', ['kill', 'message', 'isolate_notice'])->count());
        $sm->stop($practice);

        // 2. In Test mode: false-positive titles in allowlist ("Claude Shannon", "Gemini Observatory") raise NO alert.
        $testSession = $sm->start($lab, $this->profile('Exam: strict'), 'CS102');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(3, 'window_focus', ['app' => 'evince', 'title' => 'Claude Shannon - A Mathematical Theory of Communication.pdf']),
            $this->ev(4, 'window_focus', ['app' => 'firefox', 'title' => 'Gemini Observatory - Telescope Schedule']),
        ]))->assertOk();
        $this->assertSame(0, Alert::count());
        $this->assertSame(0, Command::where('type', 'kill')->count());

        // 3. In Test mode: non-allowlisted AI title raises a LOW-severity alert with confidence='weak', never kills or isolates.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(5, 'window_focus', ['app' => 'chrome', 'title' => 'ChatGPT - Google Chrome']),
        ]))->assertOk();
        $this->assertSame(0, Command::whereIn('type', ['kill', 'message', 'isolate_notice'])->count());
        $alert = Alert::sole();
        $this->assertSame('low', $alert->severity);
        $this->assertSame('web.ai.chatgpt', $alert->rule_id);
        $this->assertSame('weak', $alert->details['confidence']);
        $this->assertNotEmpty($alert->details['meaning']);
        $this->assertNotEmpty($alert->details['not_meaning']);
        $sm->stop($testSession);
    }

    public function test_c2_ide_ai_activity_and_isolation_lease_expired_events(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);
        $sm = app(SessionManager::class);

        $payload = [
            'tool' => 'github-copilot',
            'ide' => 'vscode',
            'evidence' => 'file_activity',
            'detail' => 'github.copilot-chat',
            'confidence' => 'medium',
            'meaning' => 'IDE AI extension storage was written to',
            'not_meaning' => 'Does not prove chat use; inline completions, sign-in or telemetry produce the same signal',
        ];

        // 1. In Always-on / Practice: stored as event, no alert, no popup, no kill.
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'ide_ai_activity', $payload),
        ]))->assertOk();
        $this->assertDatabaseHas('events', ['pc_id' => $pc->id, 'type' => 'ide_ai_activity']);
        $this->assertSame(0, Alert::count());

        // 2. In Test mode: creates a medium-severity alert with meaning and not_meaning preserved.
        $sm->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(2, 'ide_ai_activity', $payload),
        ]))->assertOk();

        $alert = Alert::sole();
        $this->assertSame('medium', $alert->severity);
        $this->assertSame('ide_ai', $alert->category);
        $this->assertSame($payload['meaning'], $alert->details['meaning']);
        $this->assertSame($payload['not_meaning'], $alert->details['not_meaning']);

        // Dashboard PC detail endpoint exposes meaning and not_meaning on timeline and alerts.
        $admin = $this->user('admin');
        $drawer = $this->actingAs($admin)->getJson("/pcs/{$pc->id}")->assertOk()->json();
        $this->assertSame($payload['meaning'], $drawer['alerts'][0]['details']['meaning']);
        $this->assertSame($payload['not_meaning'], $drawer['alerts'][0]['details']['not_meaning']);

        // 3. isolation_lease_expired clears PC isolated state cleanly.
        $sm->isolate($pc, true, $admin, 'manual check');
        $this->assertTrue($pc->fresh()->isolated);
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(3, 'isolation_lease_expired', ['lease_s' => 600]),
        ]))->assertOk();
        $this->assertFalse($pc->fresh()->isolated);
    }

    public function test_r11_heartbeat_lease_field_and_pc_isolation_expires_at(): void
    {
        Queue::fake();
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $h = $this->agentHeaders($token, $cert);

        config(['xephyguard.isolation_lease_seconds' => 420]);
        $res = $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, []))->assertOk()->json();
        $this->assertSame(420, $res['isolation_lease_s']);

        $this->assertNull($pc->fresh()->isolation_expires_at);
        app(SessionManager::class)->isolate($pc, true, null, 'test lease');
        $pc->refresh();
        $this->assertNotNull($pc->isolation_expires_at);
        $this->assertArrayHasKey('isolation_expires_at', $pc->tile());
        $this->assertArrayHasKey('isolation_expires_at', $pc->toArray());
    }

    public function test_r12_downloads_index_requires_staff_authentication(): void
    {
        $this->bootLab();
        $this->get('/downloads')->assertRedirect('/login');

        $assistant = $this->user('assistant');
        $this->actingAs($assistant)->get('/downloads')->assertOk()->assertSee('XephyGuard Student PC Installers');
    }

    public function test_broadcast_auth_blocks_same_hostname_privilege_escalation(): void
    {
        $this->bootLab();
        [$pc1, $token1, $cert1] = $this->enrolledPc('192.168.10.21', 'SHARED-HOST');
        [$pc2] = $this->enrolledPc('192.168.10.22', 'SHARED-HOST');
        $this->assertSame($pc1->hostname, $pc2->hostname);

        $h1 = $this->agentHeaders($token1, $cert1);
        $this->withHeaders($h1)->postJson('/api/agent/v1/broadcasting/auth', [
            'socket_id' => '111.222',
            'channel_name' => "private-pc.{$pc1->id}",
        ])->assertOk();

        // Must reject subscribing to pc2's channel even though both PCs have the same hostname.
        $this->withHeaders($h1)->postJson('/api/agent/v1/broadcasting/auth', [
            'socket_id' => '111.222',
            'channel_name' => "private-pc.{$pc2->id}",
        ])->assertForbidden();
    }

    public function test_command_bus_broadcast_policy_updated_and_phase0_seeder(): void
    {
        $this->bootLab();
        $this->enrolledPc('192.168.10.21', 'PC-01');
        $this->enrolledPc('192.168.10.22', 'PC-02');

        $sent = CommandBus::broadcastPolicyUpdated();
        $this->assertSame(2, $sent);
        $this->assertSame(2, Command::where('type', 'policy_updated')->count());

        // Verify Phase0PolicySeeder populated custom IDE domains and desktop rule exes idempotently.
        foreach (Phase0PolicySeeder::IDE_BLOCKLIST_DOMAINS as $domain) {
            $this->assertTrue(
                BlocklistEntry::where('category', 'ide')->where('value', $domain)->exists(),
                "Missing seeded IDE domain {$domain}"
            );
        }
        $desktopRule = Rule::where('rule_id', 'proc.desktop.chatgpt')->firstOrFail();
        $this->assertContains('copilotapphost', $desktopRule->definition['match']['exe']);
        $this->assertContains('github copilot', $desktopRule->definition['match']['exe']);
    }
}
