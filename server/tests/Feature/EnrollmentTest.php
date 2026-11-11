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
        $conf = app(\App\Services\CertificateAuthority::class)->opensslConfig();
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        openssl_csr_export(openssl_csr_new(['commonName' => 'lab-pc-07'], $key, ['digest_alg' => 'sha256', 'config' => $conf]), $pem);
        @unlink($conf);

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
        $parsedCert = openssl_x509_parse($creds['cert_pem']);
        $this->assertStringContainsString('TLS Web Client Authentication', $parsedCert['extensions']['extendedKeyUsage'] ?? '');
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

    public function test_cloned_machines_with_same_hostname_and_mac_stay_separate_pcs(): void
    {
        $lab = $this->bootLab();
        $svc = app(EnrollmentService::class);
        $base = ['hostname' => 'LAB-PC', 'macs' => ['aa:bb:cc:00:00:01']];
        $uidA = str_repeat('a', 32);
        $uidB = str_repeat('b', 32);

        $a = $svc->register($lab, $base + ['machine_uid' => $uidA, 'csr_pem' => $this->csr()], '10.0.0.1')['enrollment'];
        $b = $svc->register($lab, $base + ['machine_uid' => $uidB, 'csr_pem' => $this->csr()], '10.0.0.2')['enrollment'];
        // the second registration must not supersede the first pending one
        $this->assertSame('pending', $a->fresh()->status);

        $pcA = $svc->approve($a->fresh(), null);
        $pcB = $svc->approve($b->fresh(), null);

        $this->assertNotSame($pcA->id, $pcB->id);
        $this->assertSame(2, \App\Models\Pc::whereNull('retired_at')->count());
        $this->assertNotSame($pcA->name, $pcB->name);
        $this->assertSame(1, $pcA->agents()->whereNull('revoked_at')->count());
        $this->assertSame(1, $pcB->agents()->whereNull('revoked_at')->count());

        // re-enrolling machine A returns to A, never B
        $a2 = $svc->register($lab, $base + ['machine_uid' => $uidA, 'csr_pem' => $this->csr()], '10.0.0.1')['enrollment'];
        $this->assertSame($pcA->id, $svc->approve($a2->fresh(), null)->id);
        $this->assertSame(1, $pcB->agents()->whereNull('revoked_at')->count());
        $this->assertSame(2, \App\Models\Pc::whereNull('retired_at')->count());
    }

    public function test_commands_only_reach_their_own_pc_even_with_shared_hostname(): void
    {
        $lab = $this->bootLab();
        $svc = app(EnrollmentService::class);
        $pcs = [];
        foreach (['c', 'd'] as $ch) {
            $e = $svc->register($lab, ['hostname' => 'SAME', 'macs' => [], 'machine_uid' => str_repeat($ch, 32), 'csr_pem' => $this->csr()], '10.0.0.3')['enrollment'];
            $pcs[] = $svc->approve($e->fresh(), null);
        }
        $cmd = \App\Models\Command::make(['type' => 'lock']);
        $cmd->pc_id = $pcs[0]->id;
        $channels = (new \App\Events\AgentCommand($cmd))->broadcastOn();
        $this->assertCount(1, $channels);
        $this->assertSame('private-pc.'.$pcs[0]->id, $channels[0]->name);
    }

    public function test_legacy_pc_is_adopted_once_only_when_hostname_and_mac_match_uniquely(): void
    {
        $lab = $this->bootLab();
        $svc = app(EnrollmentService::class);
        $legacy = $svc->approve($svc->register($lab, ['hostname' => 'old1', 'macs' => ['aa:aa:aa:aa:aa:01'], 'csr_pem' => $this->csr()], '10.0.0.4')['enrollment'], null);

        $uid = str_repeat('e', 32);
        $e = $svc->register($lab, ['hostname' => 'OLD1', 'macs' => ['AA:AA:AA:AA:AA:01'], 'machine_uid' => $uid, 'csr_pem' => $this->csr()], '10.0.0.4')['enrollment'];
        $this->assertSame($legacy->id, $svc->approve($e->fresh(), null)->id);
        $this->assertSame($uid, $legacy->fresh()->machine_uid);

        // same hostname, different uid, same MAC: now a separate PC (legacy row already owns a uid)
        $e2 = $svc->register($lab, ['hostname' => 'old1', 'macs' => ['aa:aa:aa:aa:aa:01'], 'machine_uid' => str_repeat('f', 32), 'csr_pem' => $this->csr()], '10.0.0.5')['enrollment'];
        $this->assertNotSame($legacy->id, $svc->approve($e2->fresh(), null)->id);
        $this->assertNull($legacy->fresh()->retired_at);
    }

    public function test_enroll_api_rejects_malformed_machine_uid(): void
    {
        $lab = $this->bootLab();
        $body = ['enroll_key' => $lab->enrollKey(), 'hostname' => 'x', 'csr_pem' => $this->csr()];
        $this->postJson('/api/agent/v1/enroll', $body + ['machine_uid' => 'not-hex'])->assertStatus(422);
        $this->postJson('/api/agent/v1/enroll', $body + ['machine_uid' => str_repeat('1', 32)])->assertCreated();
    }
}
