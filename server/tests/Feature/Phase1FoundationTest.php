<?php

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\EnrollmentKey;
use App\Models\Event;
use App\Models\Lab;
use App\Models\NetEvent;
use App\Services\CertificateAuthority;
use App\Services\EventIngestor;
use App\Services\PolicyBuilder;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Artisan;
use Illuminate\Support\Facades\Queue;
use Illuminate\Support\Str;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class Phase1FoundationTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function csr(string $cn = 'lab-pc-10'): string
    {
        $key = openssl_pkey_new(['private_key_type' => OPENSSL_KEYTYPE_EC, 'curve_name' => 'prime256v1']);
        openssl_csr_export(openssl_csr_new(['commonName' => $cn], $key, ['digest_alg' => 'sha256']), $pem);

        return $pem;
    }

    private function batch(int $pcId, array $events, array $hb = []): array
    {
        return [
            'v' => 1,
            'pc_id' => (string) $pcId,
            'heartbeat' => $hb + [
                'mode' => 'test',
                'user' => 'student1',
                'agent_version' => '0.2.0',
                'local_time_ms' => now()->getTimestampMs() - 1000,
                'capabilities' => ['keys' => true],
            ],
            'events' => $events,
        ];
    }

    private function ev(int $seq, string $type, array $data): array
    {
        return ['seq' => $seq, 'ts' => now()->getTimestampMs() - 1000, 'type' => $type, 'data' => $data];
    }

    public function test_p1_c1_separate_ca_roles_signing_cert_and_name_constrained_interception_ca(): void
    {
        $this->bootLab();
        $ca = CertificateAuthority::fromConfig();

        // 1. Verify CaInit command creates/updates ca.crt, server.crt, signing.crt, interception-ca.crt, mitmproxy-ca.pem
        Artisan::call('xephyguard:ca:init', ['--force' => true]);
        $this->assertTrue($ca->exists());
        $this->assertTrue($ca->hasSigningCert());
        $this->assertTrue($ca->hasInterceptionCa());
        $this->assertFileExists($ca->mitmproxyPemPath());

        // 2. Verify signing.crt has CA:FALSE, digitalSignature, and codeSigning EKU, signed by Root CA
        $signingPem = $ca->signingCertPem();
        $this->assertNotEmpty($signingPem);
        $this->assertSame(1, openssl_x509_verify($signingPem, openssl_pkey_get_public($ca->certPem())));
        $signingInfo = openssl_x509_parse($signingPem);
        $this->assertStringContainsString('CA:FALSE', strtoupper($signingInfo['extensions']['basicConstraints'] ?? ''));
        $this->assertStringContainsString('digital signature', strtolower($signingInfo['extensions']['keyUsage'] ?? ''));
        $this->assertStringContainsString('code signing', strtolower($signingInfo['extensions']['extendedKeyUsage'] ?? ''));

        // 3. Verify signData uses signing.key and verifyData validates chain + EKU + signature
        $payload = '{"version":"2026.11.07","rules":[]}';
        $sig = $ca->signData($payload);
        $this->assertTrue($ca->verifyData($payload, $sig, $signingPem));
        $this->assertFalse($ca->verifyData($payload.'-tampered', $sig, $signingPem));

        // Agent client cert (clientAuth EKU, not codeSigning) must be rejected as a bundle signer
        [$pc, $token, $agentCertPem] = $this->enrolledPc();
        $this->assertFalse($ca->verifyData($payload, $sig, $agentCertPem));

        // 4. Verify PolicyBuilder::response includes signer_cert in signatures envelope
        $policyResp = app(PolicyBuilder::class)->response($pc);
        $this->assertSame($signingPem, $policyResp['signatures']['signer_cert']);
        $this->assertNotEmpty($policyResp['signatures']['sha256']);

        // 5. Verify interception-ca.crt is a sub-CA with pathlen:0 and critical Name Constraints
        $interceptionPem = file_get_contents($ca->interceptionCaCertPath());
        $this->assertSame(1, openssl_x509_verify($interceptionPem, openssl_pkey_get_public($ca->certPem())));
        $interceptionInfo = openssl_x509_parse($interceptionPem);
        $this->assertStringContainsString('CA:TRUE', strtoupper($interceptionInfo['extensions']['basicConstraints'] ?? ''));
        $this->assertStringContainsString('PATHLEN:0', strtoupper($interceptionInfo['extensions']['basicConstraints'] ?? ''));

        $ncExt = $interceptionInfo['extensions']['nameConstraints'] ?? '';
        $this->assertNotEmpty($ncExt, 'interception-ca.crt must carry X.509 nameConstraints extension');
        $this->assertStringContainsString('.google.com', $ncExt);
        $this->assertStringContainsString('.bing.com', $ncExt);
        $this->assertStringContainsString('.duckduckgo.com', $ncExt);
        $this->assertStringContainsString('0.0.0.0', $ncExt);
    }

    public function test_p1_c2_short_lived_revocable_enrollment_keys_lifecycle(): void
    {
        $lab = $this->bootLab();
        $admin = $this->user('admin');

        // 1. Issue a key with maxUses=2 and allowedMacs=['aa:bb:cc:dd:ee:01']
        $issued = EnrollmentKey::issue($lab, 'Row A Key', ttlHours: 12, maxUses: 2, allowedMacs: ['AA:BB:CC:DD:EE:01'], by: $admin);
        $plain = $issued['plain'];
        $model = $issued['model'];
        $this->assertStringStartsWith('XG-', $plain);

        // Disallowed MAC is rejected without consuming uses_count
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $plain,
            'hostname' => 'pc-wrong-mac',
            'macs' => ['aa:bb:cc:dd:ee:99'],
            'csr_pem' => $this->csr('pc-wrong-mac'),
        ])->assertForbidden();
        $this->assertSame(0, $model->fresh()->uses_count);

        // Allowed MAC succeeds and increments uses_count (1/2)
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $plain,
            'hostname' => 'pc-row-a1',
            'macs' => ['aa:bb:cc:dd:ee:01'],
            'csr_pem' => $this->csr('pc-row-a1'),
        ])->assertCreated();
        $this->assertSame(1, $model->fresh()->uses_count);

        // Second use succeeds (2/2)
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $plain,
            'hostname' => 'pc-row-a2',
            'macs' => ['aa:bb:cc:dd:ee:01'],
            'csr_pem' => $this->csr('pc-row-a2'),
        ])->assertCreated();
        $this->assertSame(2, $model->fresh()->uses_count);

        // Third use is rejected (max_uses exhausted)
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $plain,
            'hostname' => 'pc-row-a3',
            'macs' => ['aa:bb:cc:dd:ee:01'],
            'csr_pem' => $this->csr('pc-row-a3'),
        ])->assertForbidden();

        // 2. Expiry enforcement
        $expiring = EnrollmentKey::issue($lab, 'Short TTL Key', ttlHours: 1);
        $this->travel(2)->hours();
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $expiring['plain'],
            'hostname' => 'pc-late',
            'macs' => ['aa:bb:cc:dd:ee:02'],
            'csr_pem' => $this->csr('pc-late'),
        ])->assertForbidden();
        $this->travelBack();

        // 3. Web routes & CLI for creating and revoking keys
        $createRes = $this->actingAs($admin)->postJson('/enroll/keys', [
            'label' => 'Web Issued Key',
            'ttl_hours' => 24,
            'max_uses' => 5,
        ])->assertCreated()->json();
        $this->assertNotEmpty($createRes['key']);

        $this->actingAs($admin)->postJson("/enroll/keys/{$createRes['id']}/revoke")->assertOk();
        $this->postJson('/api/agent/v1/enroll', [
            'enroll_key' => $createRes['key'],
            'hostname' => 'pc-revoked',
            'macs' => ['aa:bb:cc:dd:ee:03'],
            'csr_pem' => $this->csr('pc-revoked'),
        ])->assertForbidden();

        // CLI --create, --list, --revoke
        Artisan::call('xephyguard:enroll-key', ['--create' => 'CLI Key', '--ttl' => 6, '--max-uses' => 3]);
        $cliPlain = trim(Artisan::output());
        $this->assertStringStartsWith('XG-', $cliPlain);
        $cliKeyModel = EnrollmentKey::where('label', 'CLI Key')->firstOrFail();

        Artisan::call('xephyguard:enroll-key', ['--list' => true]);
        $this->assertStringContainsString('CLI Key', Artisan::output());

        Artisan::call('xephyguard:enroll-key', ['--revoke' => $cliKeyModel->key_prefix]);
        $this->assertNotNull($cliKeyModel->fresh()->revoked_at);
    }

    public function test_p1_c3_evidence_classification_installed_vs_active_vs_blocked(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc('192.168.10.21', 'PC-01');
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $h = $this->agentHeaders($token, $cert);

        // Ingest installed, active, behavioral, and system events
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'rule_hit', [
                'rule_id' => 'ext.vscode.copilot',
                'severity' => 'medium',
                'category' => 'extension',
                'description' => 'GitHub Copilot extension installed',
                'subject' => 'github.copilot',
                'actions' => ['log', 'alert'],
            ]),
            $this->ev(2, 'rule_hit', [
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama server running',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'alert'],
            ]),
            $this->ev(3, 'paste', [
                'chars' => 1200,
                'lines' => 45,
                'code' => true,
                'score' => 0.85,
                'src' => 'chrome',
                'dst' => 'code',
            ]),
            $this->ev(4, 'tamper', [
                'kind' => 'clock_change',
                'delta_ms' => 120000,
            ]),
        ]))->assertOk();

        // Ship a gateway blocked NetEvent matching the PC's current IP
        $this->withToken('test-internal-token')->postJson('/api/internal/net-events', [
            'events' => [[
                'client_ip' => $pc->fresh()->ip,
                'host' => 'chatgpt.com',
                'layer' => 'sni',
                'action' => 'blocked',
                'category' => 'ai',
                'ts' => now()->getTimestampMs(),
            ]],
        ])->assertOk();

        $extAlert = Alert::where('rule_id', 'ext.vscode.copilot')->firstOrFail();
        $this->assertSame('installed', $extAlert->event_class);
        $this->assertSame('weak', $extAlert->confidence);
        $this->assertNotEmpty($extAlert->evidence_meta['what_it_means']);
        $this->assertNotEmpty($extAlert->evidence_meta['what_it_does_not_mean']);

        $procAlert = Alert::where('rule_id', 'proc.ollama')->firstOrFail();
        $this->assertSame('active', $procAlert->event_class);
        $this->assertSame('strong', $procAlert->confidence);

        $pasteAlert = Alert::where('rule_id', 'behavior.large_paste')->firstOrFail();
        $this->assertSame('behavioral', $pasteAlert->event_class);
        $this->assertSame('weak', $pasteAlert->confidence);

        $tamperAlert = Alert::where('rule_id', 'tamper.clock_change')->firstOrFail();
        $this->assertSame('system', $tamperAlert->event_class);
        $this->assertSame('strong', $tamperAlert->confidence);

        $netEv = NetEvent::firstOrFail();
        $this->assertSame('blocked', $netEv->event_class);
        $this->assertSame('strong', $netEv->confidence);

        // Verify DashboardController::pc exposes separate counts and evidence_meta
        $admin = $this->user('admin');
        $pcJson = $this->actingAs($admin)->getJson("/pcs/{$pc->id}")->assertOk()->json();
        $this->assertSame(1, $pcJson['installed_count']);
        $this->assertSame(1, $pcJson['active_count']);
        $this->assertSame(1, $pcJson['blocked_count']);
        $this->assertSame(['installed' => 1, 'active' => 1, 'blocked' => 1], $pcJson['evidence_counts']);

        // Verify AlertController filtering by event_class
        $this->actingAs($admin)->get('/alerts?event_class=installed')
            ->assertOk()
            ->assertSee('ext.vscode.copilot')
            ->assertDontSee('proc.ollama');
    }

    public function test_p1_c4_bilingual_notice_dynamic_profile_rendering(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $sm = app(SessionManager::class);

        // 1. Always-on baseline in English and Bangla
        $this->get('/notice?lang=en')
            ->assertOk()
            ->assertSee('Lab computers are monitored by XephyGuard')
            ->assertSee('Always-on')
            ->assertSee('Open Browsing (Baseline)')
            ->assertSee('Bounded Network Isolation Guarantee');

        $this->get('/notice?lang=bn')
            ->assertOk()
            ->assertSee('ল্যাব কম্পিউটারগুলো XephyGuard দ্বারা পর্যবেক্ষণ করা হচ্ছে')
            ->assertSee('অলওয়েজ-অন')
            ->assertSee('সীমাবদ্ধ নেটওয়ার্ক আইসোলেশন নিশ্চয়তা');

        // 2. Practice: AI allowed, logged -> AI-Permitted Disclosure Mode
        $practiceAllowed = $sm->start($lab, $this->profile('Practice: AI allowed, logged'), 'CS201-Lab');
        $this->get('/notice?lang=en')
            ->assertOk()
            ->assertSee('CS201-Lab')
            ->assertSee('AI-Permitted Disclosure Mode');
        $this->get('/notice?lang=bn')
            ->assertOk()
            ->assertSee('CS201-Lab')
            ->assertSee('এআই-অনুমোদিত ডিসক্লোজার মোড');
        $sm->stop($practiceAllowed);

        // 3. Exam: strict -> AI Blocking Active + Full window title disclosed
        $examStrict = $sm->start($lab, $this->profile('Exam: strict'), 'CS301-Final');
        $this->get('/notice?lang=en')
            ->assertOk()
            ->assertSee('CS301-Final')
            ->assertSee('AI Blocking Active')
            ->assertSee('Full window title');
        $this->get('/notice?lang=bn')
            ->assertOk()
            ->assertSee('CS301-Final')
            ->assertSee('এআই ব্লকিং সক্রিয়')
            ->assertSee('সম্পূর্ণ উইন্ডো টাইটেল');
        $sm->stop($examStrict);
    }

    public function test_p1_c5_observability_healthz_metrics_and_backup_restore_roundtrip(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $pc->update(['last_seen_at' => now(), 'isolated' => true]);

        // 1. /healthz returns JSON status
        $this->getJson('/healthz')
            ->assertOk()
            ->assertJsonStructure([
                'status',
                'db',
                'ca_initialized',
                'active_signature_version',
                'pcs_online',
                'pcs_isolated',
                'queue_depth',
                'timestamp',
            ])
            ->assertJson([
                'status' => 'ok',
                'db' => true,
                'ca_initialized' => true,
                'pcs_online' => 1,
                'pcs_isolated' => 1,
            ]);

        // 2. /metrics auth check & Prometheus exposition
        config(['xephyguard.metrics_token' => 'prom-secret-token']);
        $this->get('/metrics')->assertForbidden();
        $this->withToken('wrong-token')->get('/metrics')->assertForbidden();

        $metricsText = $this->withToken('prom-secret-token')
            ->get('/metrics')
            ->assertOk()
            ->getContent();

        $this->assertStringContainsString('xephyguard_pcs_total 1', $metricsText);
        $this->assertStringContainsString('xephyguard_pcs_online 1', $metricsText);
        $this->assertStringContainsString('xephyguard_pcs_isolated 1', $metricsText);
        $this->assertStringContainsString('xephyguard_events_total', $metricsText);
        $this->assertStringContainsString('xephyguard_alerts_open_total{severity="high"}', $metricsText);
        $this->assertStringContainsString('xephyguard_agents_by_version', $metricsText);

        // Authenticated staff can also read /metrics
        $admin = $this->user('admin');
        $this->actingAs($admin)->get('/metrics')->assertOk();

        // 3. Backup & Restore (--verify-only and full restore)
        $backupPath = sys_get_temp_dir().'/xg-test-backup-'.Str::random(8).'.tar.gz';
        try {
            $exitBackup = Artisan::call('xephyguard:backup', ['--output' => $backupPath]);
            $this->assertSame(0, $exitBackup);
            $this->assertFileExists($backupPath);

            $exitVerify = Artisan::call('xephyguard:restore', [
                'archive' => $backupPath,
                '--verify-only' => true,
            ]);
            $verifyOut = Artisan::output();
            $this->assertSame(0, $exitVerify, 'Verify output: '.$verifyOut);
            $this->assertStringContainsString('Verified backup archive', $verifyOut);

            // Mutate state, then restore and verify original Lab name returns
            $originalName = $lab->name;
            $lab->update(['name' => 'Mutated Lab Name']);
            $this->assertSame('Mutated Lab Name', Lab::current()->name);

            $exitRestore = Artisan::call('xephyguard:restore', ['archive' => $backupPath]);
            $this->assertSame(0, $exitRestore);
            $this->assertSame($originalName, Lab::current()->name);
        } finally {
            @unlink($backupPath);
        }
    }
}
