<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\Setting;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class UiRegressionTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_save_settings_accepts_search_template_url_and_gates_log_queries(): void
    {
        $this->bootLab();
        $admin = $this->user('admin');

        $payload = [
            'lab_name' => 'Lab Updated 51',
            'network_mode' => 'gateway',
            'lab_subnet' => '192.168.10.0/24',
            'ccc_ip' => '192.168.10.2',
            'retention_days_raw' => 120,
            'retention_days_reports' => 730,
            'paste_alert_chars' => 800,
            'title_capture_always_on' => 'app_only',
            'title_capture_practice' => 'app_only',
            'title_capture_test' => 'full',
            'notice_text' => 'Monitored for integrity.',
            'log_urls' => true,
            'log_queries' => true,
            'google_search_url' => 'https://www.google.com/search?q={searchTerms}&udm=14',
        ];

        $this->actingAs($admin)
            ->post('/settings', $payload)
            ->assertRedirect()
            ->assertSessionHasNoErrors()
            ->assertSessionHas('status', 'Settings saved.');

        $this->assertSame('Lab Updated 51', Lab::current()->name);
        $this->assertSame('https://www.google.com/search?q={searchTerms}&udm=14', Setting::get('google_search_url'));
        $logUrls = Setting::get('privacy.log_urls');
        $this->assertTrue((bool) ($logUrls['enabled'] ?? false));
        $this->assertTrue((bool) ($logUrls['query'] ?? false));

        // When URL logging is disabled, query logging must automatically turn off as well.
        $payload['log_urls'] = false;
        $payload['log_queries'] = true;
        $this->actingAs($admin)->post('/settings', $payload)->assertRedirect()->assertSessionHasNoErrors();
        $logUrls2 = Setting::get('privacy.log_urls');
        $this->assertFalse((bool) ($logUrls2['enabled'] ?? false));
        $this->assertFalse((bool) ($logUrls2['query'] ?? false));
    }

    public function test_dashboard_includes_all_52_unacknowledged_alerts_including_retired_pcs(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'Rupom2');
        $retired = Pc::create([
            'lab_id' => $lab->id,
            'name' => 'SIyam',
            'hostname' => 'siyam',
            'ip' => '192.168.10.22',
            'os' => 'Linux',
            'macs' => ['aa:bb:cc:00:00:99'],
            'retired_at' => now()->subDay(),
        ]);

        for ($i = 1; $i <= 35; $i++) {
            Alert::create([
                'pc_id' => $retired->id,
                'rule_id' => 'net.ai_domain',
                'severity' => 'high',
                'title' => "Retired alert #{$i}",
                'occurred_at' => now()->subMinutes($i),
            ]);
        }
        for ($i = 36; $i <= 52; $i++) {
            Alert::create([
                'pc_id' => $pc->id,
                'rule_id' => 'proc.vpn',
                'severity' => 'medium',
                'title' => "Live alert #{$i}",
                'occurred_at' => now()->subMinutes($i),
            ]);
        }
        for ($i = 1; $i <= 5; $i++) {
            Alert::create([
                'pc_id' => $pc->id,
                'rule_id' => 'proc.info',
                'severity' => 'low',
                'title' => "Closed alert #{$i}",
                'occurred_at' => now()->subHours($i),
                'acknowledged_at' => now()->subMinutes(10),
            ]);
        }

        $res = $this->actingAs($this->user('admin'))->get('/')->assertOk();
        $html = $res->getContent();
        preg_match('/<script type="application\/json" id="xg-live-data">(.*?)<\/script>/s', $html, $m);
        $this->assertNotEmpty($m, 'Expected #xg-live-data JSON block on dashboard.');

        $data = json_decode($m[1], true, 512, JSON_THROW_ON_ERROR);
        $unacked = array_values(array_filter($data['alerts'], fn ($a) => ! $a['acked']));
        $this->assertCount(52, $unacked);

        $retiredAlerts = array_values(array_filter($unacked, fn ($a) => $a['pcId'] === $retired->id));
        $this->assertCount(35, $retiredAlerts);
        $this->assertTrue($retiredAlerts[0]['pcRetired']);
    }

    public function test_alerts_open_filter_and_json_acknowledge_handle_retired_pcs(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $retired = Pc::create([
            'lab_id' => $lab->id,
            'name' => 'Sazzad',
            'hostname' => 'sazzad',
            'ip' => '192.168.10.23',
            'os' => 'Windows',
            'macs' => ['aa:bb:cc:00:00:88'],
            'retired_at' => now()->subDay(),
        ]);
        $open = Alert::create([
            'pc_id' => $retired->id,
            'rule_id' => 'net.ai_domain',
            'severity' => 'critical',
            'title' => 'Open retired alert',
            'occurred_at' => now()->subMinute(),
        ]);
        Alert::create([
            'pc_id' => $retired->id,
            'rule_id' => 'net.ai_domain',
            'severity' => 'low',
            'title' => 'Acked alert',
            'occurred_at' => now()->subHour(),
            'acknowledged_at' => now()->subMinutes(5),
        ]);

        $admin = $this->user('admin');
        $res = $this->actingAs($admin)->get('/alerts?open=1')->assertOk();
        preg_match('/<script type="application\/json" id="xg-page-data">(.*?)<\/script>/s', $res->getContent(), $m);
        $data = json_decode($m[1], true, 512, JSON_THROW_ON_ERROR);
        $this->assertCount(1, $data['items']);
        $this->assertSame(1, $data['openTotal']);
        $this->assertSame('Open retired alert', $data['items'][0]['title']);

        $this->actingAs($admin)
            ->postJson("/alerts/{$open->id}/ack", ['comment' => 'Verified'])
            ->assertOk()
            ->assertJson(['ok' => true]);

        $this->assertNotNull($open->fresh()->acknowledged_at);
    }

    public function test_csp_compliance_no_inline_executable_scripts_on_web_routes(): void
    {
        Queue::fake();
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $admin = $this->user('admin');
        $this->actingAs($admin);

        foreach (['/', '/alerts', '/sessions', '/enroll', '/profiles', '/reports', '/roster', '/signatures',
            '/health', '/settings', '/users', '/audit', '/notice', '/downloads', '/share'] as $url) {
            $html = $this->get($url)->assertOk()->getContent();
            // Strip script[src] and script[type="application/json"] and confirm no other <script> tags remain.
            $stripped = preg_replace('/<script\b[^>]*\bsrc\s*=[^>]*>.*?<\/script>/is', '', $html);
            $stripped = preg_replace('/<script\b[^>]*\btype=["\']application\/json["\'][^>]*>.*?<\/script>/is', '', $stripped);
            $this->assertDoesNotMatchRegularExpression('/<script\b/i', $stripped, "Found inline executable <script> on $url");
        }
    }

    public function test_downloads_layout_handles_guest_no_rail_and_kb_sizes(): void
    {
        $this->bootLab();
        $dir = storage_path('app/public/downloads');
        @mkdir($dir, 0755, true);
        $smallFile = $dir.'/test-signing-cert.cer';
        file_put_contents($smallFile, str_repeat('A', 900));

        try {
            $guestHtml = $this->get('/downloads')->assertOk()->getContent();
            $this->assertStringContainsString('class="body no-rail"', $guestHtml);
            $this->assertStringContainsString('1 KB · Code signing certificate', $guestHtml);
            $this->assertStringNotContainsString('0 MB · Release artifact', $guestHtml);
            $this->assertStringContainsString('Connection credentials', $guestHtml);
            preg_match('/<script type="application\/json" id="xg-page-data">(.*?)<\/script>/s', $guestHtml, $dm);
            $dlData = json_decode($dm[1], true, 512, JSON_THROW_ON_ERROR);
            $this->assertNotEmpty($dlData['cccHost']);
            $this->assertArrayNotHasKey('key', $dlData);                 // S-15: no enrollment key on the public page
            $this->assertStringContainsString('/KEY=<ENROLL_KEY>', $dlData['win']);
            $this->assertNotEmpty($dlData['fp']);
            $this->assertStringContainsString('/CAFP='.$dlData['fp'], $dlData['win']);
            $this->assertStringContainsString('/CCC='.$dlData['cccHost'], $dlData['win']);

            $admin = $this->user('admin');
            $authHtml = $this->actingAs($admin)->get('/downloads')->assertOk()->getContent();
            $this->assertStringContainsString('<div class="body">', $authHtml);
            $this->assertStringContainsString('aria-label="Downloads"', $authHtml);
            $this->assertMatchesRegularExpression('/href="[^"]*\/downloads"[^>]*aria-current="page"/', $authHtml);
        } finally {
            @unlink($smallFile);
        }
    }

    public function test_acknowledging_all_alerts_turns_seat_green_and_resets_live_score(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'Rupom2');
        $pc->update(['current_user' => 'student.rupom', 'status' => 'alert', 'live_score' => 100, 'last_seen_at' => now()]);

        $crit = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'tamper.x', 'severity' => 'critical', 'title' => 'Crit', 'occurred_at' => now()->subMinutes(4)]);
        $high = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'net.ai_domain', 'severity' => 'high', 'title' => 'High', 'occurred_at' => now()->subMinutes(3)]);
        $med = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'net.wifi_new', 'severity' => 'medium', 'title' => 'Med', 'occurred_at' => now()->subMinutes(2)]);
        $low = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'proc.info', 'severity' => 'low', 'title' => 'Low', 'occurred_at' => now()->subMinute()]);
        Alert::create(['pc_id' => $pc->id, 'rule_id' => 'net.ai_domain', 'severity' => 'high', 'title' => 'Old acked', 'occurred_at' => now()->subMinutes(10), 'acknowledged_at' => now()->subMinutes(5)]);

        $admin = $this->user('admin');
        $this->actingAs($admin);

        // Batch-acknowledge critical + high: only medium (15) + low (5) = 20 remain -> warning/watch (amber)
        $this->postJson('/alerts/ack-batch', ['ids' => [$crit->id, $high->id]])
            ->assertOk()
            ->assertJsonPath('pcs.0.status', 'warning')
            ->assertJsonPath('pcs.0.score', 20);

        $dashMid = $this->get('/')->assertOk()->getContent();
        preg_match('/<script type="application\/json" id="xg-live-data">(.*?)<\/script>/s', $dashMid, $m1);
        $midData = json_decode($m1[1], true, 512, JSON_THROW_ON_ERROR);
        $midPc = collect($midData['pcs'])->firstWhere('id', $pc->id);
        $this->assertSame('watch', $midPc['status']);
        $this->assertSame(20, $midPc['score']);

        // Ack medium: only low (5) remains -> clean/normal (green)
        $this->postJson("/alerts/{$med->id}/ack", [])
            ->assertOk()
            ->assertJsonPath('pc.status', 'clean')
            ->assertJsonPath('pc.score', 5);

        // Ack low: zero unacked alerts remain -> clean/normal, score = 0
        $this->postJson("/alerts/{$low->id}/ack", [])
            ->assertOk()
            ->assertJsonPath('pc.status', 'clean')
            ->assertJsonPath('pc.score', 0);

        $pc->refresh();
        $this->assertSame('clean', $pc->status);
        $this->assertSame(0, $pc->live_score);

        $dashClean = $this->get('/')->assertOk()->getContent();
        preg_match('/<script type="application\/json" id="xg-live-data">(.*?)<\/script>/s', $dashClean, $m2);
        $cleanData = json_decode($m2[1], true, 512, JSON_THROW_ON_ERROR);
        $cleanPc = collect($cleanData['pcs'])->firstWhere('id', $pc->id);
        $this->assertSame('normal', $cleanPc['status']);
        $this->assertSame(0, $cleanPc['score']);
    }

    public function test_dashboard_reconciles_stale_live_score_when_no_open_alerts_exist(): void
    {
        Queue::fake();
        $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'RUPOM');
        // Simulate stale DB state where alerts were acknowledged under older code leaving live_score = 100
        $pc->update(['current_user' => 'rupam_e6wsmfr', 'status' => 'clean', 'live_score' => 100, 'last_seen_at' => now()]);
        Alert::create(['pc_id' => $pc->id, 'rule_id' => 'web.ai_tab', 'severity' => 'high', 'title' => 'Acked ChatGPT', 'occurred_at' => now()->subMinutes(5), 'acknowledged_at' => now()->subMinute()]);

        $html = $this->actingAs($this->user('admin'))->get('/')->assertOk()->getContent();
        preg_match('/<script type="application\/json" id="xg-live-data">(.*?)<\/script>/s', $html, $m);
        $data = json_decode($m[1], true, 512, JSON_THROW_ON_ERROR);
        $renderedPc = collect($data['pcs'])->firstWhere('id', $pc->id);

        $this->assertSame('normal', $renderedPc['status']);
        $this->assertSame(0, $renderedPc['score']);
        $this->assertSame(0, $pc->fresh()->live_score);
        $this->assertSame('clean', $pc->fresh()->status);
    }

    public function test_rescan_endpoint_dispatches_specific_target_for_what_and_kind(): void
    {
        Queue::fake();
        $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $user = $this->user('assistant');

        foreach (['extensions', 'artifacts', 'network'] as $target) {
            $this->actingAs($user)
                ->postJson("/pcs/{$pc->id}/rescan", ['what' => $target])
                ->assertOk()
                ->assertJsonStructure(['command_id']);
            $this->assertDatabaseHas('commands', ['pc_id' => $pc->id, 'type' => 'rescan', 'args->what' => $target]);
        }

        // Legacy `kind` payload compatibility
        $this->actingAs($user)
            ->postJson("/pcs/{$pc->id}/rescan", ['kind' => 'devices'])
            ->assertOk();
        $this->assertDatabaseHas('commands', ['pc_id' => $pc->id, 'type' => 'rescan', 'args->what' => 'devices']);
    }

    public function test_views_cache_bust_app_js_with_filemtime(): void
    {
        Queue::fake();
        $this->bootLab();
        $mtime = (string) filemtime(public_path('js/app.js'));
        $expected = 'js/app.js?v='.$mtime;

        foreach (['/login', '/notice'] as $guestUrl) {
            $html = $this->get($guestUrl)->assertOk()->getContent();
            $this->assertStringContainsString($expected, $html, "Missing cache-busted app.js on $guestUrl");
        }

        $dashHtml = $this->actingAs($this->user('admin'))->get('/')->assertOk()->getContent();
        $this->assertStringContainsString($expected, $dashHtml);
    }
}
