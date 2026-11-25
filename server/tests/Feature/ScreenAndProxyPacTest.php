<?php

namespace Tests\Feature;

use App\Models\LabSession;
use App\Models\Profile;
use App\Models\Setting;
use App\Services\NetworkPolicyWriter;
use App\Services\PolicyService;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ScreenAndProxyPacTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_policy_carries_no_screen_gateway_address_or_token(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();

        config(['services.screen_gateway.url' => 'https://lab.example.edu/screen-gw']);
        $policy = app(PolicyService::class)->response($pc);
        $this->assertArrayNotHasKey('screen', $policy);
        $this->assertStringNotContainsString('agent_token', json_encode($policy));
    }

    public function test_admin_sets_screen_cpu_budget_and_policy_carries_it(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $form = ['lab_name' => 'Lab', 'network_mode' => 'proxy_only', 'ccc_ip' => '192.168.10.1', 'lab_subnet' => '192.168.10.0/24',
            'retention_days_raw' => 30, 'retention_days_reports' => 365, 'notice_text' => 'Monitored.', 'paste_alert_chars' => 800,
            'title_capture_always_on' => 'off', 'title_capture_practice' => 'app_only', 'title_capture_test' => 'app_only'];
        $admin = $this->user('admin');

        $this->actingAs($admin)->post('/settings', $form + ['screen_cpu_budget_pct' => 0])->assertSessionHasErrors('screen_cpu_budget_pct');
        $this->actingAs($admin)->post('/settings', $form + ['screen_cpu_budget_pct' => 101])->assertSessionHasErrors('screen_cpu_budget_pct');
        $this->assertNull(Setting::get('screen.cpu_budget_pct'));

        $this->actingAs($admin)->post('/settings', $form + ['screen_cpu_budget_pct' => 12])->assertSessionHasNoErrors();
        $this->assertSame(12, app(PolicyService::class)->forPc($pc)['screen']['cpu_budget_pct']);
        $this->actingAs($admin)->get('/settings')->assertOk()->assertSee('name="screen_cpu_budget_pct"', false);
    }

    public function test_proxy_pac_reflects_network_mode_and_policy_carries_pac_url(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();

        Setting::put('network_mode', 'gateway');
        Setting::put('ccc_ip', '192.168.10.2');
        $this->get('/proxy.pac')->assertOk()->assertSee('return "DIRECT";', false);

        Setting::put('network_mode', 'proxy_only');
        $this->get('/proxy.pac')
            ->assertOk()
            ->assertHeader('Content-Type', 'application/x-ns-proxy-autoconfig; charset=utf-8')
            ->assertSee('PROXY 192.168.10.2:8080; DIRECT', false);

        $pol = app(PolicyService::class)->forPc($pc);
        $this->assertSame('proxy_only', $pol['proxy_mode']);
        $this->assertSame('https://192.168.10.2/proxy.pac', $pol['proxy_pac_url']);
    }

    public function test_proxy_pac_has_no_direct_fallback_for_pcs_in_a_test_session(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21');
        Setting::put('network_mode', 'proxy_only');
        Setting::put('ccc_ip', '192.168.10.2');
        $fromPc = fn () => $this->withServerVariables(['REMOTE_ADDR' => '192.168.10.21'])->get('/proxy.pac');

        $fromPc()->assertOk()->assertSee('PROXY 192.168.10.2:8080; DIRECT', false);   // always-on: fallback kept
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'EXAM');
        $fromPc()->assertOk()->assertSee('return "PROXY 192.168.10.2:8080";', false);  // test: fail closed
        $this->withServerVariables(['REMOTE_ADDR' => '192.168.10.99'])->get('/proxy.pac')
            ->assertSee('PROXY 192.168.10.2:8080; DIRECT', false);                        // unknown host: unchanged
    }

    public function test_session_network_lock_reaches_the_policy_in_proxy_only_mode(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21');
        $pb = app(PolicyService::class);
        Setting::put('network_mode', 'proxy_only');
        Setting::put('ccc_ip', '192.168.10.2');
        $this->assertArrayNotHasKey('network_lock', $pb->forPc($pc));   // always-on: never locked

        $assistant = $this->user('assistant');
        $this->actingAs($assistant)->post('/sessions', ['course' => 'EXAM',
            'profile_id' => $this->profile('Test: AI blocked')->id])->assertRedirect();
        $session = LabSession::activeFor($lab->id);
        $this->assertSame('observe', $pb->forPc($pc)['network_lock']);  // default

        $before = $pb->forPc($pc)['version'];
        $this->actingAs($assistant)->post("/sessions/{$session->id}/network-lock", ['network_lock' => 'strict'])
            ->assertRedirect()->assertSessionHasNoErrors();
        $this->assertSame('strict', $pb->forPc($pc)['network_lock']);
        $this->assertNotSame($before, $pb->forPc($pc)['version']);       // agents re-fetch on the next ack
        $this->assertDatabaseHas('audit_log', ['action' => 'session.network_lock']);
        $this->actingAs($assistant)->post("/sessions/{$session->id}/network-lock", ['network_lock' => 'everything'])
            ->assertSessionHasErrors('network_lock');
        $this->assertSame('strict', $session->fresh()->network_lock);

        // The sessions page offers the control only in proxy-only mode.
        $this->actingAs($assistant)->get('/sessions')->assertOk()->assertSee('Network: strict (proxy only)');
        Setting::put('network_mode', 'gateway');
        $this->assertArrayNotHasKey('network_lock', $pb->forPc($pc));   // the gateway enforces instead
        $this->actingAs($assistant)->get('/sessions')->assertOk()->assertDontSee('Network: strict (proxy only)');

        Setting::put('network_mode', 'proxy_only');
        $this->actingAs($assistant)->post('/sessions', ['course' => 'EXAM2', 'network_lock' => 'off',
            'profile_id' => $this->profile('Test: AI blocked')->id])->assertRedirect();
        $this->assertSame('off', $pb->forPc($pc)['network_lock']);
    }

    public function test_always_on_udm14_populates_udm14_clients_and_saving_profile_writes_net_state(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.55', 'PC-ALWAYS-ON');
        $admin = $this->user('admin');

        // Default Always-on has google_udm14 = false
        app(NetworkPolicyWriter::class)->write();
        $state = $this->netState();
        $this->assertSame([], $state['google']['udm14_clients']);
        $this->assertFalse($state['google']['force_udm14']);

        // Enabling google_udm14 on Always-on triggers immediate write & populates udm14_clients without creating blocking groups
        $alwaysOn = Profile::query()->where('name', 'Always-on')->firstOrFail();
        $this->actingAs($admin)->post(route('profiles.save', $alwaysOn), [
            'name' => $alwaysOn->name,
            'mode' => $alwaysOn->mode,
            'description' => $alwaysOn->description,
            'google_udm14' => '1',
            'title_capture' => 'app_only',
            'collect' => ['window', 'clipboard', 'keys'],
        ])->assertRedirect();

        $updated = $this->netState();
        $this->assertContains('192.168.10.55', $updated['google']['udm14_clients']);
        $this->assertTrue($updated['google']['force_udm14']);
        $this->assertSame([], $updated['groups']['test']);
        $this->assertSame([], $updated['groups']['block_ai']);

        // Unchecking google_udm14 turns it off immediately
        $this->actingAs($admin)->post(route('profiles.save', $alwaysOn), [
            'name' => $alwaysOn->name,
            'mode' => $alwaysOn->mode,
            'description' => $alwaysOn->description,
            'title_capture' => 'app_only',
            'collect' => ['window', 'clipboard', 'keys'],
        ])->assertRedirect();

        $cleared = $this->netState();
        $this->assertSame([], $cleared['google']['udm14_clients']);
        $this->assertFalse($cleared['google']['force_udm14']);
    }
}
