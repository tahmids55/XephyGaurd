<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Events\AgentCommand;
use App\Models\Command;
use App\Models\Enrollment;
use App\Models\Pc;
use App\Services\CertificateAuthority;
use App\Services\EnrollmentService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class EnrollmentTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function csr(): string
    {
        $conf = app(CertificateAuthority::class)->opensslConfig();
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        openssl_csr_export(openssl_csr_new(['commonName' => 'lab-pc-07'], $key, ['digest_alg' => 'sha256', 'config' => $conf]), $pem);
        @unlink($conf);

        return $pem;
    }

    public function test_approve_dont_configure_flow(): void
    {
        $lab = $this->bootLab();
        $body = ['enroll_key' => $lab->enrollKey(), 'hostname' => 'lab-pc-07', 'os' => 'Linux',
            'macs' => ['aa:bb:cc:dd:ee:07'], 'csr_pem' => $this->csr(), 'agent_version' => '0.1.0', 'machine_uid' => str_repeat('07', 16)];

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

    public function test_known_mac_never_auto_approves_and_reenrollment_keeps_the_pc(): void
    {
        $lab = $this->bootLab();
        // Old F16 settings left in the DB must have no effect (S-16).
        $lab->update(['auto_approve' => true, 'auto_approve_macs' => ['aa:bb:cc:dd:ee:09']]);
        $svc = app(EnrollmentService::class);
        $staff = $this->user('assistant');
        $first = $svc->register($lab, ['hostname' => 'pc9', 'macs' => ['AA:BB:CC:DD:EE:09'], 'machine_uid' => str_repeat('09', 16), 'csr_pem' => $this->csr()], '192.168.10.9');
        $this->assertSame('pending', $first['enrollment']->status);
        $this->assertSame(0, Pc::count());
        $pc = $svc->approve($first['enrollment'], $staff);
        $second = $svc->register($lab, ['hostname' => 'pc9', 'macs' => ['aa:bb:cc:dd:ee:09'], 'machine_uid' => str_repeat('09', 16), 'csr_pem' => $this->csr()], '192.168.10.9');
        $this->assertSame('pending', $second['enrollment']->status);
        $this->assertSame($pc->id, $svc->approve($second['enrollment'], $staff)->id);
        $this->assertSame(1, $pc->agents()->whereNull('revoked_at')->count());
        $this->assertSame(2, Enrollment::count());
        $this->actingAs($this->user('admin'))->post('/enroll/auto-approve', ['auto_approve' => 1])->assertNotFound();
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
        $this->assertSame(2, Pc::whereNull('retired_at')->count());
        $this->assertNotSame($pcA->name, $pcB->name);
        $this->assertSame(1, $pcA->agents()->whereNull('revoked_at')->count());
        $this->assertSame(1, $pcB->agents()->whereNull('revoked_at')->count());

        // re-enrolling machine A returns to A, never B
        $a2 = $svc->register($lab, $base + ['machine_uid' => $uidA, 'csr_pem' => $this->csr()], '10.0.0.1')['enrollment'];
        $this->assertSame($pcA->id, $svc->approve($a2->fresh(), null)->id);
        $this->assertSame(1, $pcB->agents()->whereNull('revoked_at')->count());
        $this->assertSame(2, Pc::whereNull('retired_at')->count());
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
        $cmd = Command::make(['type' => 'lock']);
        $cmd->pc_id = $pcs[0]->id;
        $channels = (new AgentCommand($cmd))->broadcastOn();
        $this->assertCount(1, $channels);
        $this->assertSame('private-pc.'.$pcs[0]->id, $channels[0]->name);
    }

    public function test_look_alike_pc_is_flagged_never_merged_and_adopted_only_by_staff_choice(): void
    {
        $lab = $this->bootLab();
        $svc = app(EnrollmentService::class);
        $staff = $this->user('assistant');
        $legacy = $svc->approve($svc->register($lab, ['hostname' => 'old1', 'macs' => ['aa:aa:aa:aa:aa:01'], 'csr_pem' => $this->csr()], '10.0.0.4')['enrollment'], $staff);

        // Same hostname + MAC, new uid: flagged, and a plain approve makes a NEW PC (no silent merge, no retire).
        $uid = str_repeat('e', 32);
        $e = $svc->register($lab, ['hostname' => 'OLD1', 'macs' => ['AA:AA:AA:AA:AA:01'], 'machine_uid' => $uid, 'csr_pem' => $this->csr()], '10.0.0.4')['enrollment'];
        $this->assertSame($legacy->id, $e->fresh()->conflict_pc_id);
        $this->actingAs($staff)->get('/enroll')->assertOk()->assertSee('"conflict":{"id":'.$legacy->id, false);
        $new = $svc->approve($e->fresh(), $staff);
        $this->assertNotSame($legacy->id, $new->id);
        $this->assertNull($legacy->fresh()->retired_at);
        $this->assertNull($legacy->fresh()->machine_uid);

        // Explicit staff choice adopts the uid-less row.
        $uid2 = str_repeat('d', 32);
        $e2 = $svc->register($lab, ['hostname' => 'old1', 'macs' => ['aa:aa:aa:aa:aa:01'], 'machine_uid' => $uid2, 'csr_pem' => $this->csr()], '10.0.0.5')['enrollment'];
        $this->actingAs($staff)->postJson("/enroll/{$e2->id}/approve", ['adopt_pc_id' => $legacy->id])->assertOk();
        $this->assertSame($uid2, $legacy->fresh()->machine_uid);
        $this->assertDatabaseHas('audit_log', ['action' => 'enrollment.approve', 'subject_id' => $e2->id]);

        // A PC that already has another uid can never be adopted.
        $e3 = $svc->register($lab, ['hostname' => 'x3', 'machine_uid' => str_repeat('c', 32), 'csr_pem' => $this->csr()], '10.0.0.6')['enrollment'];
        $this->actingAs($staff)->postJson("/enroll/{$e3->id}/approve", ['adopt_pc_id' => $legacy->id])->assertStatus(422);
        $this->assertSame('pending', $e3->fresh()->status);
        $this->assertSame($uid2, $legacy->fresh()->machine_uid);
    }

    public function test_enroll_api_rejects_malformed_machine_uid(): void
    {
        $lab = $this->bootLab();
        $body = ['enroll_key' => $lab->enrollKey(), 'hostname' => 'x', 'csr_pem' => $this->csr()];
        $this->postJson('/api/agent/v1/enroll', $body)->assertStatus(422);                              // required (S-17)
        $this->postJson('/api/agent/v1/enroll', $body + ['machine_uid' => 'not-hex'])->assertStatus(422);
        $this->postJson('/api/agent/v1/enroll', $body + ['machine_uid' => str_repeat('1', 32)])->assertCreated();
    }
}
