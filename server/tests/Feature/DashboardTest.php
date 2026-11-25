<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Models\SignatureVersion;
use App\Services\CertificateAuthority;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class DashboardTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_first_run_wizard_then_login(): void
    {
        $this->get('/')->assertRedirect('/setup');
        $this->get('/setup')->assertOk()->assertSee('Welcome to XephyGuard');
        config(['xephyguard.net_state_path' => sys_get_temp_dir().'/xg-wiz/state.json']);
        $this->post('/setup', ['name' => 'Ada', 'email' => 'ada@lab.test', 'password' => 'long-enough-pw', 'password_confirmation' => 'long-enough-pw',
            'lab_name' => 'Lab 3', 'network_mode' => 'gateway', 'lab_subnet' => '192.168.10.0/24', 'ccc_ip' => '192.168.10.2',
            'retention_days_raw' => 120, 'notice_text' => 'Monitored.'])->assertRedirect('/enroll');
        $this->assertAuthenticated();
        $this->get('/setup')->assertNotFound();   // wizard is single-use
        $this->post('/logout');
        $this->post('/login', ['email' => 'ada@lab.test', 'password' => 'wrong'])->assertSessionHasErrors('email');
        $this->post('/login', ['email' => 'ada@lab.test', 'password' => 'long-enough-pw'])->assertRedirect('/');
    }

    public function test_every_screen_renders_for_admin(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $admin = $this->user('admin');
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        app(SessionManager::class)->stop($s);
        app(SessionManager::class)->start($lab, $this->profile('Practice: AI allowed, logged'), 'CS102');
        $this->actingAs($admin);
        foreach (['/', '/alerts', '/sessions', '/enroll', '/profiles', '/reports', "/reports/{$s->id}", '/roster', '/signatures',
            '/health', '/settings', '/users', '/audit', '/notice'] as $url) {
            $this->get($url)->assertOk();
        }
        $this->getJson("/pcs/{$pc->id}")->assertOk()->assertJsonStructure(['pc', 'timeline', 'keyrate']);
        $this->get('/ca.crt')->assertOk()->assertSee('BEGIN CERTIFICATE');
        $this->get('/install/linux.sh')->assertOk()->assertSee('xephyguard-agent enroll');
        $this->get("/reports/{$s->id}/csv")->assertOk();
    }

    public function test_roles_are_enforced_server_side(): void
    {
        $this->bootLab();
        $assistant = $this->user('assistant');
        $instructor = $this->user('instructor');
        $this->user('admin');
        $this->actingAs($assistant)->get('/')->assertOk();
        $this->actingAs($assistant)->get('/reports')->assertForbidden();
        $this->actingAs($assistant)->get('/signatures')->assertForbidden();
        $this->actingAs($assistant)->post('/settings', [])->assertForbidden();
        $this->actingAs($instructor)->get('/reports')->assertOk();
        $this->actingAs($instructor)->get('/users')->assertForbidden();
        $instructor->update(['active' => false]);
        $this->actingAs($instructor->fresh())->get('/reports')->assertForbidden();
    }

    public function test_audit_log_is_append_only(): void
    {
        $this->bootLab();
        $log = AuditLog::create(['action' => 'x']);
        $this->expectException(\LogicException::class);
        $log->update(['action' => 'y']);
    }

    public function test_blocklist_edit_hot_reloads_network_state(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'))->post('/blocklist', ['category' => 'ai', 'value' => 'https://new-ai-thing.example/'])->assertRedirect();
        $this->assertContains('new-ai-thing.example', $this->netState()['categories']['ai']);
        $this->post('/blocklist', ['category' => 'ai', 'value' => 'not a domain'])->assertStatus(422);
    }

    public function test_signature_publish_and_rollback(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'));
        $this->post('/rules', ['rule_id' => 'proc.newtool', 'kind' => 'process', 'severity' => 'high', 'description' => 'New tool',
            'definition' => json_encode(['match' => ['exe' => ['newtool']]])])->assertRedirect();
        $this->post('/signatures/publish', ['note' => 'add newtool'])->assertRedirect();
        $active = SignatureVersion::active();
        $this->assertStringContainsString('proc.newtool', $active->bundle);
        $this->assertTrue(app(CertificateAuthority::class)->verifyData($active->bundle, $active->signature));
        $first = SignatureVersion::orderBy('id')->first();
        $this->post("/signatures/{$first->id}/activate")->assertRedirect();
        $this->assertSame($first->id, SignatureVersion::active()->id);
    }

    public function test_download_allows_bat_scripts(): void
    {
        $dir = storage_path('app/public/downloads');
        @mkdir($dir, 0755, true);
        $path = $dir.'/test-helper.bat';
        file_put_contents($path, "@echo off\r\n");
        try {
            $this->get('/downloads/test-helper.bat')->assertOk();
            $this->get('/downloads/invalid.txt')->assertNotFound();
        } finally {
            @unlink($path);
        }
    }

    public function test_downloads_serves_deb_and_generates_http_linux_installer_command(): void
    {
        $lab = $this->bootLab();
        $lab->update(['ccc_ip' => '192.168.10.2']);
        $dir = storage_path('app/public/downloads');
        @mkdir($dir, 0755, true);
        $deb = $dir.'/xephyguard-agent_test_amd64.deb';
        file_put_contents($deb, 'deb-stub');
        try {
            $this->get('/downloads/xephyguard-agent_test_amd64.deb')->assertOk();
            $res = $this->actingAs($this->user('assistant'))
                ->get('https://192.168.10.2/downloads')
                ->assertOk();
            $res->assertSee('xephyguard-agent_test_amd64.deb');
            preg_match('/<script type="application\/json" id="xg-page-data">(.*?)<\/script>/s', $res->getContent(), $m);
            $page = json_decode($m[1] ?? '{}', true, 512, JSON_THROW_ON_ERROR);
            $this->assertStringStartsWith('curl -fsSL http://192.168.10.2/install/linux.sh', $page['lin'] ?? '');
            // S-15: the lab key never appears on /downloads, not even for logged-in staff.
            $this->assertStringNotContainsString($lab->enrollKey(), $res->getContent());
            $this->assertArrayNotHasKey('key', $page);
        } finally {
            @unlink($deb);
        }
    }
}
