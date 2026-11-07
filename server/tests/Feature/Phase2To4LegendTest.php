<?php

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Appeal;
use App\Models\AuditLog;
use App\Models\Command;
use App\Models\Event;
use App\Models\KeyStat;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Score;
use App\Models\Setting;
use App\Models\Student;
use App\Models\TwoPersonRequest;
use App\Services\PolicyBuilder;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Artisan;
use Illuminate\Support\Facades\Queue;
use Illuminate\Support\Str;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class Phase2To4LegendTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

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

    public function test_p2_c1_student_transparency_portal_and_appeals_workflow(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $student = Student::create([
            'student_number' => '2026001',
            'name' => 'Amina Rahman',
            'login' => 'student1',
        ]);
        $portalToken = $student->ensurePortalToken();
        $this->assertSame(48, strlen($portalToken));

        $session = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS205-Midterm');
        $h = $this->agentHeaders($token, $cert);

        // Ingest events that raise an alert and record file_activity
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'file_activity', [
                'user' => 'student1',
                'dir' => '~/lab/midterm',
                'files_changed' => 3,
                'total_bytes' => 4200,
            ]),
            $this->ev(2, 'rule_hit', [
                'user' => 'student1',
                'rule_id' => 'ext.vscode.copilot',
                'severity' => 'medium',
                'category' => 'extension',
                'description' => 'GitHub Copilot extension installed',
                'subject' => 'github.copilot',
                'actions' => ['log', 'alert'],
            ]),
        ]))->assertOk();

        $alert = Alert::where('rule_id', 'ext.vscode.copilot')->firstOrFail();
        $score = Score::create([
            'lab_session_id' => $session->id,
            'pc_id' => $pc->id,
            'student_id' => $student->id,
            'user' => 'student1',
            'method' => 'v1',
            'score' => 25,
            'confidence' => 'low',
            'reasons' => [['label' => 'Copilot extension installed', 'points' => 15]],
        ]);

        // 1. Access Student Transparency Portal (HTML & JSON & Export)
        $this->flushHeaders();
        $this->get('/student-portal')
            ->assertOk()
            ->assertSee('Access Your Transparency Record');

        $this->get("/student-portal?token={$portalToken}")
            ->assertOk()
            ->assertSee('Amina Rahman')
            ->assertSee('CS205-Midterm')
            ->assertSee('ext.vscode.copilot')
            ->assertSee($alert->evidence_meta['what_it_means'])
            ->assertSee($alert->evidence_meta['what_it_does_not_mean']);

        $portalJson = $this->getJson("/student-portal?token={$portalToken}")
            ->assertOk()
            ->json();
        $this->assertSame('2026001', $portalJson['student']['student_number']);
        $this->assertCount(1, $portalJson['alerts']);
        $this->assertCount(1, $portalJson['work_journal']);

        $exportRes = $this->get("/student-portal/export?token={$portalToken}")->assertOk();
        $this->assertStringContainsString('attachment; filename="xephyguard-student-data-2026001.json"', (string) $exportRes->headers->get('Content-Disposition'));

        // 2. Submit an appeal from the student portal -> triggers automatic legal_hold on Student & LabSession
        $this->assertFalse($student->fresh()->legal_hold);
        $this->assertFalse($session->fresh()->legal_hold);

        $appealRes = $this->postJson('/student-portal/appeals', [
            'token' => $portalToken,
            'lab_session_id' => $session->id,
            'alert_id' => $alert->id,
            'score_id' => $score->id,
            'student_statement' => 'Copilot extension was installed from last semester software engineering course and was disabled in workspace.',
        ])->assertCreated()->json();

        $appealId = $appealRes['appeal']['id'];
        $this->assertTrue($student->fresh()->legal_hold);
        $this->assertTrue($session->fresh()->legal_hold);
        $this->assertTrue(AuditLog::where('action', 'appeal.submitted')->where('subject_id', $appealId)->exists());

        // Create a second appeal (to test upheld vs overturned rate calculation)
        $appeal2 = Appeal::create([
            'lab_session_id' => $session->id,
            'student_id' => $student->id,
            'status' => 'open',
            'student_statement' => 'Secondary question on session timing.',
        ]);

        // 3. Instructor reviews and decides appeals
        $instructor = $this->user('instructor');
        $this->actingAs($instructor)->postJson("/appeals/{$appealId}/decide", [
            'status' => 'overturned',
            'decision_reason' => 'Verified extension was installed-only inventory and never active during the exam.',
        ])->assertOk();

        // Alert is marked acknowledged with [Overturned]
        $this->assertNotNull($alert->fresh()->acknowledged_at);
        $this->assertStringContainsString('[Overturned]', (string) $alert->fresh()->comment);
        // Legal hold remains true while second appeal is still open
        $this->assertTrue($session->fresh()->legal_hold);
        $this->assertTrue($student->fresh()->legal_hold);

        // Decide second appeal as upheld -> no open appeals remain, so automatic legal_hold clears
        $this->actingAs($instructor)->postJson("/appeals/{$appeal2->id}/decide", [
            'status' => 'upheld',
            'decision_reason' => 'Session timing was accurate per NTP sync.',
        ])->assertOk();

        $this->assertFalse($session->fresh()->legal_hold);
        $this->assertFalse($student->fresh()->legal_hold);
        $this->assertTrue(AuditLog::where('action', 'appeal.decided')->where('subject_id', $appealId)->exists());

        // 4. Verify institutional overturn rate (1 overturned / 2 decided = 0.5) and rule breakdown
        $appealsJson = $this->actingAs($instructor)->getJson('/appeals')->assertOk()->json();
        $this->assertSame(2, $appealsJson['decided_total']);
        $this->assertSame(1, $appealsJson['overturned_count']);
        $this->assertEqualsWithDelta(0.5, $appealsJson['overturn_rate'], 0.001);
        $this->assertSame(1, $appealsJson['overturned_by_rule']['ext.vscode.copilot'] ?? 0);

        $this->actingAs($instructor)->get('/appeals')
            ->assertOk()
            ->assertSee('50.0%')
            ->assertSee('ext.vscode.copilot');
    }

    public function test_p2_c2_dpia_and_irb_ethics_review_pack_web_and_cli(): void
    {
        $this->bootLab();
        Setting::put('lab_name', 'BUET CSE Lab 3');
        Setting::put('retention_days_raw', 120);

        $admin = $this->user('admin');
        $instructor = $this->user('instructor');

        $this->actingAs($instructor)->get('/ethics-pack')->assertForbidden();

        $this->actingAs($admin)->get('/ethics-pack')
            ->assertOk()
            ->assertSee('BUET CSE Lab 3')
            ->assertSee('Data Protection Impact Assessment (DPIA)');

        $mdResponse = $this->actingAs($admin)->get('/ethics-pack?format=md')->assertOk();
        $mdText = $mdResponse->getContent();
        $this->assertStringContainsString('BUET CSE Lab 3', $mdText);
        $this->assertStringContainsString('120 days', $mdText);
        $this->assertStringContainsString('GDPR', $mdText);
        $this->assertStringContainsString('FERPA', $mdText);
        $this->assertStringContainsString('Informed Consent for Pseudonymized Educational Research', $mdText);

        $outFile = sys_get_temp_dir().'/xg-ethics-pack-'.Str::random(8).'.md';
        try {
            $exit = Artisan::call('xephyguard:ethics-pack', ['--output' => $outFile]);
            $this->assertSame(0, $exit);
            $this->assertFileExists($outFile);
            $cliContent = file_get_contents($outFile);
            $this->assertStringContainsString('BUET CSE Lab 3', $cliContent);
            $this->assertStringContainsString('Tiered Data Collection Matrix', $cliContent);
        } finally {
            @unlink($outFile);
        }
    }

    public function test_p2_c3_scheduled_retention_purge_with_cryptographic_proof_and_legal_hold(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $profile = $this->profile('Test: AI blocked');

        $normalOldSession = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $profile->id,
            'course' => 'CS101-Old',
            'mode' => 'test',
            'status' => 'ended',
            'legal_hold' => false,
            'starts_at' => now()->subDays(160),
            'ended_at' => now()->subDays(160),
        ]);

        $heldOldSession = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $profile->id,
            'course' => 'CS102-Held',
            'mode' => 'test',
            'status' => 'ended',
            'legal_hold' => true,
            'starts_at' => now()->subDays(160),
            'ended_at' => now()->subDays(160),
        ]);

        $heldStudent = Student::create([
            'student_number' => '2026999',
            'name' => 'Held Student',
            'login' => 'held_user',
            'legal_hold' => true,
        ]);

        $oldTs = now()->subDays(160);
        $recentTs = now()->subDays(10);

        // Old unprotected rows (should be purged)
        Event::create(['pc_id' => $pc->id, 'agent_id' => 1, 'seq' => 1, 'lab_session_id' => $normalOldSession->id, 'type' => 'heartbeat', 'occurred_at' => $oldTs, 'received_at' => $oldTs, 'user' => 'regular_user', 'data' => []]);
        KeyStat::create(['pc_id' => $pc->id, 'lab_session_id' => $normalOldSession->id, 'user' => 'regular_user', 'window_start' => $oldTs, 'window_s' => 5, 'keys' => 20, 'backspaces' => 2]);
        PasteEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $normalOldSession->id, 'user' => 'regular_user', 'is_copy' => false, 'chars' => 100, 'lines' => 4, 'occurred_at' => $oldTs]);
        NetEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $normalOldSession->id, 'client_ip' => $pc->ip, 'host' => 'chatgpt.com', 'layer' => 'dns', 'action' => 'blocked', 'category' => 'ai', 'occurred_at' => $oldTs]);

        // Old legal-hold session & legal-hold student rows (MUST be preserved)
        Event::create(['pc_id' => $pc->id, 'agent_id' => 1, 'seq' => 2, 'lab_session_id' => $heldOldSession->id, 'type' => 'heartbeat', 'occurred_at' => $oldTs, 'received_at' => $oldTs, 'user' => 'regular_user', 'data' => []]);
        Event::create(['pc_id' => $pc->id, 'agent_id' => 1, 'seq' => 3, 'lab_session_id' => $normalOldSession->id, 'type' => 'heartbeat', 'occurred_at' => $oldTs, 'received_at' => $oldTs, 'user' => $heldStudent->login, 'data' => []]);
        KeyStat::create(['pc_id' => $pc->id, 'lab_session_id' => $heldOldSession->id, 'user' => 'regular_user', 'window_start' => $oldTs, 'window_s' => 5, 'keys' => 30, 'backspaces' => 1]);
        PasteEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $heldOldSession->id, 'user' => 'regular_user', 'is_copy' => false, 'chars' => 200, 'lines' => 8, 'occurred_at' => $oldTs]);
        NetEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $heldOldSession->id, 'client_ip' => $pc->ip, 'host' => 'claude.ai', 'layer' => 'sni', 'action' => 'blocked', 'category' => 'ai', 'occurred_at' => $oldTs]);

        // Recent row (MUST be preserved)
        Event::create(['pc_id' => $pc->id, 'agent_id' => 1, 'seq' => 4, 'lab_session_id' => $normalOldSession->id, 'type' => 'heartbeat', 'occurred_at' => $recentTs, 'received_at' => $recentTs, 'user' => 'regular_user', 'data' => []]);

        // 1. Dry-run does not delete
        Artisan::call('xephyguard:purge', ['--days' => 140, '--dry-run' => true]);
        $this->assertSame(4, Event::count());

        // 2. Real purge deletes only old unprotected rows and preserves legal-hold + recent rows
        Artisan::call('xephyguard:purge', ['--days' => 140]);
        $this->assertSame(3, Event::count());
        $this->assertSame(1, KeyStat::count());
        $this->assertSame(1, PasteEvent::count());
        $this->assertSame(1, NetEvent::count());

        $audit = AuditLog::where('action', 'retention.purge')->latest('id')->firstOrFail();
        $this->assertSame(1, $audit->details['deleted']['events']);
        $this->assertSame(1, $audit->details['deleted']['key_stats']);
        $this->assertSame(1, $audit->details['deleted']['paste_events']);
        $this->assertSame(1, $audit->details['deleted']['net_events']);
        $this->assertContains($heldOldSession->id, $audit->details['skipped_legal_hold_session_ids']);
        $this->assertSame(64, strlen($audit->details['proof_sha256'] ?? ''));
    }

    public function test_p2_c4_instructor_session_preflight_wizard(): void
    {
        $this->bootLab();
        [$pc1] = $this->enrolledPc('192.168.10.21', 'PC-01');
        [$pc2] = $this->enrolledPc('192.168.10.22', 'PC-02');

        $pc1->update(['last_seen_at' => now(), 'isolated' => false]);
        $pc1->agent->update(['version' => '0.2.0']);

        $pc2->update(['last_seen_at' => now()->subMinutes(10), 'isolated' => true]);
        $pc2->agent->update(['version' => '0.1.0']);

        $instructor = $this->user('instructor');
        $pf = $this->actingAs($instructor)->getJson('/sessions/preflight')->assertOk()->json();

        $this->assertFalse($pf['ready']);
        $this->assertSame(2, $pf['pcs_total']);
        $this->assertSame(1, $pf['pcs_online']);
        $this->assertSame(1, $pf['pcs_offline']);
        $this->assertSame(1, $pf['agents_current']);
        $this->assertSame(1, $pf['agents_outdated']);
        $this->assertTrue($pf['ca_healthy']);
        $this->assertNotEmpty($pf['active_signature_version']);
        $this->assertSame(1, $pf['isolated_pcs']);

        // Bring PC-02 up to date and online -> preflight becomes ready
        $pc2->update(['last_seen_at' => now(), 'isolated' => false]);
        $pc2->agent->update(['version' => '0.2.0']);

        $pfReady = $this->actingAs($instructor)->getJson('/sessions/preflight')->assertOk()->json();
        $this->assertTrue($pfReady['ready']);
        $this->assertSame(2, $pfReady['pcs_online']);
        $this->assertSame(0, $pfReady['agents_outdated']);

        $this->actingAs($instructor)->get('/sessions')
            ->assertOk()
            ->assertSee('Session Pre-Flight Check')
            ->assertSee('READY');
    }

    public function test_p3_c1_two_person_rule_and_lti_13_roster_sync(): void
    {
        $this->bootLab();
        Setting::put('two_person_rule', true);

        $instructor1 = $this->user('instructor');
        $instructor2 = $this->user('instructor');
        $student = Student::create([
            'student_number' => '2026500',
            'name' => 'Tanvir Ahmed',
            'login' => 'tanvir',
        ]);

        // 1. Instructor 1 requests student exemption -> creates pending TwoPersonRequest, does not exempt yet
        $res = $this->actingAs($instructor1)->postJson("/roster/{$student->id}/exempt", [
            'exempt' => true,
            'reason' => 'Approved screen-reader accommodation',
        ])->assertStatus(202)->json();

        $reqId = $res['two_person_request_id'];
        $this->assertFalse($student->fresh()->exempt);
        $this->assertSame('pending', TwoPersonRequest::findOrFail($reqId)->status);

        // 2. Instructor 1 cannot self-approve their own request
        $this->actingAs($instructor1)->postJson("/two-person/{$reqId}/approve")
            ->assertStatus(422);
        $this->assertFalse($student->fresh()->exempt);

        // 3. Instructor 2 (second distinct user) approves -> executes exemption & logs both users in AuditLog
        $this->actingAs($instructor2)->postJson("/two-person/{$reqId}/approve")
            ->assertOk();

        $this->assertTrue($student->fresh()->exempt);
        $this->assertSame('Approved screen-reader accommodation', $student->fresh()->exempt_reason);
        $tpReq = TwoPersonRequest::findOrFail($reqId);
        $this->assertSame('approved', $tpReq->status);
        $this->assertSame($instructor2->id, $tpReq->approved_by);

        $audit = AuditLog::where('action', 'two_person.approved')->where('subject_id', $reqId)->firstOrFail();
        $this->assertSame($instructor1->id, $audit->details['requested_by']);
        $this->assertSame($instructor2->id, $audit->details['approved_by']);

        // 4. Moodle LTI 1.3 NRPS Roster & Session Sync via bearer token
        auth()->forgetGuards();
        config(['xephyguard.lti_sync_token' => 'lti-secret-sync-token']);
        $this->postJson('/integrations/lti/roster', ['members' => []])->assertForbidden();

        $ltiRes = $this->withToken('lti-secret-sync-token')->postJson('/integrations/lti/roster', [
            'members' => [
                ['student_number' => '2026701', 'name' => 'Nusrat Jahan', 'login' => 'nusrat'],
                ['user_id' => '2026702', 'given_name' => 'Rafiq', 'family_name' => 'Islam', 'lis_person_sourcedid' => 'rafiq'],
            ],
            'session' => [
                'course' => 'CSE310-Lab',
                'title' => 'Compiler Lab Quiz 2',
                'profile' => 'Test: AI permitted with disclosure',
                'ai_policy' => 'permitted_with_disclosure',
            ],
        ])->assertOk()->json();

        $this->assertSame(2, $ltiRes['synced']);
        $nusrat = Student::where('student_number', '2026701')->firstOrFail();
        $this->assertSame(48, strlen($nusrat->portal_token));
        $this->assertSame('CSE310-Lab', $ltiRes['session']['course']);
        $this->assertSame('permitted_with_disclosure', $ltiRes['session']['ai_policy']);
    }

    public function test_p4_c1_ai_permitted_with_disclosure_mode_and_process_work_journal(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc('192.168.10.21', 'PC-01');
        $student = Student::create([
            'student_number' => '2026801',
            'name' => 'Farhana Karim',
            'login' => 'student1',
        ]);

        $disclosureProfile = $this->profile('Test: AI permitted with disclosure');
        $disclosureProfile->update([
            'ai_policy' => 'permitted_with_disclosure',
            'target_agent_version' => '0.2.5',
        ]);

        $session = app(SessionManager::class)->start($lab, $disclosureProfile, 'CSE412-OpenAI-Project');
        $this->assertSame('permitted_with_disclosure', $session->ai_policy);

        // 1. Verify PolicyBuilder::forPc includes ai_policy, target_agent_version, does NOT block AI domains, and strips kill/warn
        $policy = app(PolicyBuilder::class)->forPc($pc);
        $this->assertSame('permitted_with_disclosure', $policy['ai_policy']);
        $this->assertSame('0.2.5', $policy['target_agent_version']);
        $this->assertNotContains('chatgpt.com', $policy['blocked_domains']);
        foreach ($policy['actions']['rules'] ?? [] as $acts) {
            $this->assertNotContains('kill', $acts);
            $this->assertNotContains('warn', $acts);
        }

        // 2. Ingest work-folder file_activity and AI tool events during permitted_with_disclosure session
        $h = $this->agentHeaders($token, $cert);
        $this->withHeaders($h)->postJson('/api/agent/v1/events', $this->batch($pc->id, [
            $this->ev(1, 'file_activity', [
                'user' => 'student1',
                'dir' => '~/lab/project',
                'files_changed' => 2,
                'total_bytes' => 1850,
            ]),
            $this->ev(2, 'rule_hit', [
                'user' => 'student1',
                'rule_id' => 'proc.ollama',
                'severity' => 'high',
                'category' => 'local_llm',
                'description' => 'Ollama local model runtime',
                'subject' => 'ollama',
                'actions' => ['log', 'warn', 'kill', 'alert'],
            ]),
            $this->ev(3, 'ide_ai_activity', [
                'user' => 'student1',
                'tool' => 'copilot-chat',
                'ide' => 'vscode',
                'evidence' => 'file_activity',
                'detail' => 'chat session log updated',
            ]),
            $this->ev(4, 'file_activity', [
                'user' => 'student1',
                'dir' => '~/lab/project',
                'files_changed' => 5,
                'total_bytes' => 6400,
            ]),
        ]))->assertOk();

        // Zero punitive alerts, zero kill/warn commands
        $this->assertSame(0, Alert::where('lab_session_id', $session->id)->count());
        $this->assertSame(0, Command::where('pc_id', $pc->id)->whereIn('type', ['kill', 'message'])->count());

        // Events are classified as event_class = 'disclosure', confidence = 'medium'
        $ollamaEvent = Event::where('lab_session_id', $session->id)->where('seq', 2)->firstOrFail();
        $this->assertSame('disclosure', $ollamaEvent->event_class);
        $this->assertSame('medium', $ollamaEvent->confidence);

        // 3. Instructor fetches Process & AI Disclosure Work Journal
        $instructor = $this->user('instructor');
        $journal = $this->actingAs($instructor)
            ->getJson("/sessions/{$session->id}/pcs/{$pc->id}/journal")
            ->assertOk()
            ->json();

        $this->assertSame(2, $journal['file_saves_count']);
        $this->assertSame(2, $journal['disclosed_ai_count']);
        $this->assertCount(4, $journal['entries']);
        $this->assertSame('work_progress', $journal['entries'][0]['kind']);
        $this->assertSame('ai_disclosure', $journal['entries'][1]['kind']);

        // 4. Verify bilingual /notice displays permitted_with_disclosure status
        $this->flushHeaders();
        $this->get('/notice?lang=en')
            ->assertOk()
            ->assertSee('AI-Permitted Disclosure Mode')
            ->assertSee('permitted_with_disclosure');

        $this->get('/notice?lang=bn')
            ->assertOk()
            ->assertSee('এআই-অনুমোদিত ডিসক্লোজার মোড')
            ->assertSee('permitted_with_disclosure');
    }
}
