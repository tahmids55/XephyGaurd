<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Models\User;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\TestCase;

/** S-9 / I-SERVER-22, I-CCC-6: from the LAN the first-run wizard needs XG_SETUP_TOKEN; loopback does not. */
class SetupTokenTest extends TestCase
{
    use RefreshDatabase;

    private const TOKEN = 'a3f1c9e07b2d4e6f8a1b3c5d7e9f0a2b4c6d8e0f1a3b5c7d';

    private const LAN = ['REMOTE_ADDR' => '192.168.10.50'];

    protected function setUp(): void
    {
        parent::setUp();
        config(['xephyguard.net_state_path' => sys_get_temp_dir().'/xg-wiz-token/state.json']);
    }

    private function form(array $extra = []): array
    {
        return $extra + ['name' => 'Ada', 'email' => 'ada@lab.test', 'password' => 'long-enough-pw', 'password_confirmation' => 'long-enough-pw',
            'lab_name' => 'Lab 3', 'network_mode' => 'proxy_only', 'lab_subnet' => '192.168.10.0/24', 'ccc_ip' => '192.168.10.2',
            'retention_days_raw' => 120, 'notice_text' => 'Monitored.'];
    }

    public function test_lan_client_without_or_with_wrong_token_cannot_create_the_admin(): void
    {
        config(['xephyguard.setup_token' => self::TOKEN]);
        $this->withServerVariables(self::LAN);

        $this->get('/setup')->assertOk()->assertSee('name="setup_token"', false);
        $this->post('/setup', $this->form())->assertSessionHasErrors('setup_token');
        $this->post('/setup', $this->form(['setup_token' => 'wrong']))->assertSessionHasErrors('setup_token');
        $this->post('/setup', $this->form(['setup_token' => ['x']]))->assertSessionHasErrors('setup_token');
        $this->assertSame(0, User::count());
        $this->assertGuest();
        $this->assertArrayNotHasKey('setup_token', session()->getOldInput());
        $this->assertArrayNotHasKey('password', session()->getOldInput());
    }

    public function test_lan_client_with_the_token_completes_setup_once(): void
    {
        config(['xephyguard.setup_token' => self::TOKEN]);
        $this->withServerVariables(self::LAN);

        $this->post('/setup', $this->form(['setup_token' => self::TOKEN]))->assertRedirect('/enroll');
        $this->assertSame(1, User::query()->where('role', 'admin')->count());
        $this->assertSame('token', AuditLog::query()->where('action', 'setup.complete')->firstOrFail()->details['via'] ?? null);
        $this->post('/logout');
        $this->post('/setup', $this->form(['setup_token' => self::TOKEN, 'email' => 'eve@lab.test']))->assertNotFound();
    }

    public function test_lan_client_is_refused_when_no_or_a_short_token_is_configured(): void
    {
        $this->withServerVariables(self::LAN);
        foreach ([null, '', 'short'] as $configured) {
            config(['xephyguard.setup_token' => $configured]);
            $this->post('/setup', $this->form(['setup_token' => (string) $configured]))->assertSessionHasErrors('setup_token');
        }
        $this->assertSame(0, User::count());
    }

    public function test_loopback_behind_an_http_proxy_still_needs_the_token(): void
    {
        config(['xephyguard.setup_token' => self::TOKEN]);

        $this->withHeader('X-Forwarded-For', '192.168.10.50')->post('/setup', $this->form())->assertSessionHasErrors('setup_token');
        $this->assertSame(0, User::count());
    }

    public function test_direct_loopback_needs_no_token(): void
    {
        config(['xephyguard.setup_token' => null]);

        $this->get('/setup')->assertOk()->assertDontSee('name="setup_token"', false);
        $this->post('/setup', $this->form())->assertRedirect('/enroll');
        $this->assertSame('loopback', AuditLog::query()->where('action', 'setup.complete')->firstOrFail()->details['via'] ?? null);
    }
}
