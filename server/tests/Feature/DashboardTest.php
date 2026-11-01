<?php

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Models\LabSession;
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
        $active = \App\Models\SignatureVersion::active();
        $this->assertStringContainsString('proc.newtool', $active->bundle);
        $this->assertTrue(app(\App\Services\CertificateAuthority::class)->verifyData($active->bundle, $active->signature));
        $first = \App\Models\SignatureVersion::orderBy('id')->first();
        $this->post("/signatures/{$first->id}/activate")->assertRedirect();
        $this->assertSame($first->id, \App\Models\SignatureVersion::active()->id);
    }
}
