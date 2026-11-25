<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\EnrollmentToken;
use App\Models\Pc;
use App\Services\CertificateAuthority;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class EnrollmentTokenTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function body(string $host = 'lab-pc-31'): array
    {
        $conf = app(CertificateAuthority::class)->opensslConfig();
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1', 'config' => $conf]);
        openssl_csr_export(openssl_csr_new(['commonName' => $host], $key, ['digest_alg' => 'sha256', 'config' => $conf]), $pem);
        @unlink($conf);

        return ['hostname' => $host, 'os' => 'Linux', 'macs' => ['aa:bb:cc:dd:ee:31'], 'csr_pem' => $pem,
            'machine_uid' => str_repeat('ab', 16)];
    }

    public function test_staff_create_token_on_enroll_page_and_it_auto_approves_once(): void
    {
        $this->bootLab();
        $this->post('/enroll/tokens', ['pc_name' => 'PC-31'])->assertRedirect('/login');
        $staff = $this->user('assistant');
        $this->actingAs($staff)->post('/enroll/tokens', ['pc_name' => 'PC-31', 'hours' => 24])->assertRedirect()
            ->assertSessionHas('enroll_token');
        $plain = session('enroll_token');
        $row = EnrollmentToken::firstOrFail();
        $this->assertSame(hash('sha256', $plain), $row->token_hash);   // only the hash is stored
        $this->assertDatabaseHas('audit_log', ['action' => 'enrollment.token_create', 'subject_id' => $row->id]);
        $this->actingAs($staff)->get('/enroll')->assertOk()->assertSee($plain);              // flashed once
        $this->actingAs($staff)->get('/enroll')->assertOk()->assertSee('PC-31')->assertDontSee($plain);

        $r = $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plain] + $this->body())
            ->assertCreated()->assertJson(['status' => 'approved']);
        $this->assertSame('PC-31', Pc::findOrFail(EnrollmentToken::firstOrFail()->enrollment?->pc_id ?? 0)->name);
        $this->assertNotNull(EnrollmentToken::firstOrFail()->used_at);
        $this->assertDatabaseHas('audit_log', ['action' => 'enrollment.approve', 'subject_id' => $r->json('enrollment_id')]);

        // One-time: a second PC with the same token is refused.
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plain] + $this->body('lab-pc-32'))->assertForbidden();
    }

    public function test_lab_key_alone_stays_pending_and_bad_tokens_are_refused(): void
    {
        $lab = $this->bootLab();
        $this->postJson('/api/agent/v1/enroll', ['enroll_key' => $lab->enrollKey()] + $this->body())
            ->assertCreated()->assertJson(['status' => 'pending']);

        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => 'xgen_'.str_repeat('a', 40)] + $this->body())->assertForbidden();
        $this->postJson('/api/agent/v1/enroll', $this->body())->assertUnprocessable();          // neither key nor token

        [$expired, $plainExpired] = EnrollmentToken::issue($lab, null, null, 1);
        $expired->forceFill(['expires_at' => now()->subMinute()])->save();
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plainExpired] + $this->body())->assertForbidden();

        [$revoked, $plainRevoked] = EnrollmentToken::issue($lab, null, null, 24);
        $this->actingAs($this->user('assistant'))->post("/enroll/tokens/{$revoked->id}/revoke")->assertRedirect();
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plainRevoked] + $this->body())->assertForbidden();
        $this->assertSame(0, Pc::count());
    }

    public function test_rotating_the_lab_key_revokes_unused_tokens_only(): void
    {
        $lab = $this->bootLab();
        [$used, $plainUsed] = EnrollmentToken::issue($lab, null, 'PC-41', 24);
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plainUsed] + $this->body('lab-pc-41'))->assertCreated();
        [, $plainUnused] = EnrollmentToken::issue($lab, null, null, 24);

        $this->actingAs($this->user('admin'))->post('/enroll/rotate-key')->assertRedirect();
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plainUnused] + $this->body('lab-pc-42'))->assertForbidden();
        $this->assertNull($used->fresh()->revoked_at);                  // history of the used one is kept
        $this->assertSame(1, EnrollmentToken::whereNotNull('revoked_at')->count());
        $this->assertDatabaseHas('audit_log', ['action' => 'enroll.rotate_key']);

        // CLI rotation (xg:enroll-key --rotate) goes through the same model method.
        [, $plainCli] = EnrollmentToken::issue($lab, null, null, 24);
        $lab->fresh()->rotateEnrollKey();
        $this->postJson('/api/agent/v1/enroll', ['enroll_token' => $plainCli] + $this->body('lab-pc-43'))->assertForbidden();
    }
}
