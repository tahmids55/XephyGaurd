<?php

namespace Tests\Feature;

use App\Models\Enrollment;
use App\Services\EnrollmentService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class EnrollmentTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function csr(): string
    {
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        openssl_csr_export(openssl_csr_new(['commonName' => 'lab-pc-07'], $key, ['digest_alg' => 'sha256']), $pem);

        return $pem;
    }

    public function test_approve_dont_configure_flow(): void
    {
        $lab = $this->bootLab();
        $body = ['enroll_key' => $lab->enrollKey(), 'hostname' => 'lab-pc-07', 'os' => 'Linux',
            'macs' => ['aa:bb:cc:dd:ee:07'], 'csr_pem' => $this->csr(), 'agent_version' => '0.1.0'];

        $this->postJson('/api/agent/v1/enroll', ['enroll_key' => 'XG-WRONG'] + $body)->assertForbidden();
        $r = $this->postJson('/api/agent/v1/enroll', $body)->assertCreated()->assertJson(['status' => 'pending']);
        $id = $r->json('enrollment_id');
        $poll = $r->json('poll_token');

        $this->getJson("/api/agent/v1/enroll/$id?poll_token=wrong")->assertForbidden();
        $this->getJson("/api/agent/v1/enroll/$id?poll_token=$poll")->assertJson(['status' => 'pending']);

        $admin = $this->user('assistant');
        $this->actingAs($admin)->post("/enroll/$id/approve", ['name' => 'A3'])->assertRedirect();

        $creds = $this->getJson("/api/agent/v1/enroll/$id?poll_token=$poll")->assertOk()->json();
        $this->assertSame('approved', $creds['status']);
        $this->assertSame('A3', $creds['name']);
        $this->assertSame(1, openssl_x509_verify($creds['cert_pem'], openssl_pkey_get_public($creds['ca_pem'])));
        $this->assertStringContainsString('TLS Web Client Authentication', shell_exec('echo '.escapeshellarg($creds['cert_pem']).' | openssl x509 -noout -text'));
        // the token is handed out exactly once
        $this->getJson("/api/agent/v1/enroll/$id?poll_token=$poll")->assertJson(['status' => 'collected']);
        // ... and works with the issued certificate
        $this->withHeaders($this->agentHeaders($creds['token'], $creds['cert_pem']))->getJson('/api/agent/v1/policy')
            ->assertOk()->assertJsonPath('policy.mode', 'always_on')->assertJsonPath('realtime.channel', 'private-pc.'.$creds['pc_id']);
    }

    public function test_auto_approve_by_mac_and_reenrollment_keeps_the_pc(): void
    {
        $lab = $this->bootLab();
        $lab->update(['auto_approve' => true, 'auto_approve_macs' => ['aa:bb:cc:dd:ee:09']]);
        $svc = app(EnrollmentService::class);
        $first = $svc->register($lab, ['hostname' => 'pc9', 'macs' => ['AA:BB:CC:DD:EE:09'], 'csr_pem' => $this->csr()], '192.168.10.9');
        $this->assertSame('approved', $first['enrollment']->status);
        $second = $svc->register($lab, ['hostname' => 'pc9', 'macs' => ['aa:bb:cc:dd:ee:09'], 'csr_pem' => $this->csr()], '192.168.10.9');
        $this->assertSame($first['enrollment']->pc_id, $second['enrollment']->pc_id);
        $this->assertSame(1, $first['enrollment']->pc->agents()->whereNull('revoked_at')->count());
        $this->assertSame(2, Enrollment::count());
    }

    public function test_key_rotation_invalidates_old_key(): void
    {
        $lab = $this->bootLab();
        $old = $lab->enrollKey();
        $this->actingAs($this->user('admin'))->post('/enroll/rotate-key')->assertRedirect();
        $this->assertFalse($lab->fresh()->checkEnrollKey($old));
        $this->assertTrue($lab->fresh()->checkEnrollKey($lab->fresh()->enrollKey()));
    }
}
