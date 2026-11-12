<?php

namespace Tests\Feature;

use App\Models\Profile;
use App\Models\Setting;
use App\Services\NetworkPolicyWriter;
use App\Services\PolicyBuilder;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ScreenAndProxyPacTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_policy_builder_screen_config_handles_direct_port_and_tls_reverse_proxy(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();

        config([
            'services.screen_gateway.url' => '',
            'services.screen_gateway.port' => 8082,
            'services.screen_gateway.path' => '/screen-gw',
            'services.screen_gateway.agent_secret' => 'sec-123',
        ]);
        $direct = app(PolicyBuilder::class)->response($pc);
        $this->assertSame(8082, $direct['screen']['port']);
        $this->assertSame('/agent', $direct['screen']['path']);
        $this->assertFalse($direct['screen']['tls']);
        $this->assertSame('sec-123', $direct['screen']['agent_token']);

        config([
            'services.screen_gateway.url' => 'https://lab.example.edu/screen-gw',
            'services.screen_gateway.port' => 443,
            'services.screen_gateway.path' => '/screen-gw',
        ]);
        $proxied = app(PolicyBuilder::class)->response($pc);
        $this->assertSame(443, $proxied['screen']['port']);
        $this->assertSame('/screen-gw/agent', $proxied['screen']['path']);
        $this->assertTrue($proxied['screen']['tls']);
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

        $pol = app(PolicyBuilder::class)->forPc($pc);
        $this->assertSame('proxy_only', $pol['proxy_mode']);
        $this->assertSame('http://192.168.10.2/proxy.pac', $pol['proxy_pac_url']);
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
