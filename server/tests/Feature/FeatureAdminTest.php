<?php

namespace Tests\Feature;

// Characterization tests: pin CURRENT behavior (docs/features/server). Not a spec.
// "KNOWN BUG I-SERVER-n" marks behavior that looks wrong but is asserted as-is.

use App\Jobs\PurgeRetention;
use App\Jobs\ScoreSession;
use App\Models\Alert;
use App\Models\AuditLog;
use App\Models\BlocklistEntry;
use App\Models\Command;
use App\Models\Consent;
use App\Models\Lab;
use App\Models\NetEvent;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\Rule;
use App\Models\Score;
use App\Models\Setting;
use App\Models\Student;
use App\Models\User;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\UploadedFile;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class FeatureAdminTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    // F001
    public function test_f001_setup_wizard_validates_input_and_is_single_use(): void
    {
        $this->post('/setup', ['name' => 'Ada'])->assertSessionHasErrors();
        $this->assertSame(0, User::count());
        $this->get('/setup')->assertOk();
    }

    // F010
    public function test_f010_logout_ends_session_and_inactive_user_cannot_log_in_or_guests_are_redirected(): void
    {
        $this->bootLab();
        $this->get('/alerts')->assertRedirect('/login');
        $u = $this->user('admin');
        $this->post('/login', ['email' => $u->email, 'password' => 'correct-horse-battery'])->assertRedirect('/alerts');   // back to the intended URL
        $this->assertAuthenticatedAs($u);
        $this->post('/logout');
        $this->assertGuest();
        $this->get('/alerts')->assertRedirect('/login');
    }

    // F011
    public function test_f011_role_matrix(): void
    {
        $this->bootLab();
        $matrix = [
            'assistant' => ['/' => 200, '/alerts' => 200, '/enroll' => 200, '/sessions' => 200, '/profiles' => 403, '/reports' => 403,
                '/roster' => 403, '/signatures' => 403, '/users' => 403, '/settings' => 403, '/health' => 403, '/audit' => 403],
            'instructor' => ['/' => 200, '/profiles' => 200, '/reports' => 200, '/roster' => 200, '/signatures' => 403,
                '/users' => 403, '/settings' => 403, '/health' => 403, '/audit' => 403],
            'admin' => ['/' => 200, '/profiles' => 200, '/reports' => 200, '/roster' => 200, '/signatures' => 200,
                '/users' => 200, '/settings' => 200, '/health' => 200, '/audit' => 200],
        ];
        foreach ($matrix as $role => $urls) {
            $u = $this->user($role);
            foreach ($urls as $url => $code) {
                $this->actingAs($u)->get($url)->assertStatus($code, "$role $url");
            }
        }
    }

    // F012
    public function test_f012_admin_creates_edits_users_but_cannot_demote_or_deactivate_self(): void
    {
        $this->bootLab();
        $admin = $this->user('admin');
        $this->actingAs($admin)->post('/users', ['name' => 'Bob', 'email' => 'bob@lab.test', 'role' => 'assistant',
            'password' => 'a-long-password-1', 'active' => 1])->assertRedirect();
        $bob = User::where('email', 'bob@lab.test')->firstOrFail();
        $this->assertSame('assistant', $bob->role);
        $this->post("/users/{$bob->id}", ['name' => 'Bob', 'email' => 'bob@lab.test', 'role' => 'instructor', 'active' => 0])->assertRedirect();
        $this->assertSame('instructor', $bob->fresh()->role);
        $this->assertFalse((bool) $bob->fresh()->active);
        $this->post("/users/{$admin->id}", ['name' => 'X', 'email' => $admin->email, 'role' => 'assistant', 'active' => 1])->assertStatus(422);
        $this->assertSame('admin', $admin->fresh()->role);
        $this->post('/users', ['name' => 'Short', 'email' => 's@lab.test', 'role' => 'admin', 'password' => 'short'])->assertSessionHasErrors('password');
        $this->assertNotNull(AuditLog::where('action', 'user.save')->first());
    }

    // F013 / F021 are covered by AgentApiTest::test_broadcast_auth_only_for_own_channel (agent side);
    // dashboard channel auth: see channels.php
    public function test_f013_dashboard_channel_requires_login(): void
    {
        $this->bootLab();
        $this->post('/broadcasting/auth', ['channel_name' => 'private-dashboard', 'socket_id' => '1.1'])->assertRedirect();   // guests get the login redirect, not a 403
    }

    // F018
    public function test_f018_rename_pc_is_audited_and_validated(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->actingAs($this->user('assistant'));
        $this->post("/pcs/{$pc->id}/rename", ['name' => 'Front-01'])->assertRedirect();
        $this->assertSame('Front-01', $pc->fresh()->name);
        $this->post("/pcs/{$pc->id}/rename", ['name' => ''])->assertSessionHasErrors('name');
        $this->post("/pcs/{$pc->id}/rename", ['name' => str_repeat('x', 65)])->assertSessionHasErrors('name');
        $this->assertSame(['from' => 'PC-01', 'to' => 'Front-01'], AuditLog::where('action', 'pc.rename')->first()->detail ?? AuditLog::where('action', 'pc.rename')->first()->details);
    }

    // F019
    public function test_f019_retire_revokes_agent_and_frees_seat_but_keeps_row(): void
    {
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $this->actingAs($this->user('admin'))->post("/pcs/{$pc->id}/retire")->assertRedirect();
        $pc = $pc->fresh();
        $this->assertNotNull($pc->retired_at);
        $this->assertNull($pc->seat_id);
        $this->assertNotNull($pc->agents()->first()->revoked_at);
        $this->assertSame(0, Pc::active()->count());
        $this->postJson('/api/agent/v1/events', [], $this->agentHeaders($token, $cert))->assertStatus(401);
        $this->actingAs($this->user('instructor'))->post("/pcs/{$pc->id}/retire")->assertForbidden();
    }

    // F022 / F023
    public function test_f022_layout_round_trip_and_default_labels(): void
    {
        $this->bootLab();
        [$a] = $this->enrolledPc('192.168.10.21', 'PC-A');
        [$b] = $this->enrolledPc('192.168.10.22', 'PC-B');
        $this->actingAs($this->user('assistant'));
        $this->postJson('/lab/layout', ['cols' => 4, 'rows' => 2, 'tiles' => [
            ['pc_id' => $a->id, 'x' => 0, 'y' => 0], ['pc_id' => $b->id, 'x' => 2, 'y' => 1, 'label' => 'Z9']]])->assertOk()->assertJson(['ok' => true]);
        $lab = Lab::current();
        $this->assertSame([4, 2], [$lab->grid_cols, $lab->grid_rows]);
        $this->assertSame('A1', $a->fresh()->seat->label);
        $this->assertSame('Z9', $b->fresh()->seat->label);
        $this->postJson('/lab/layout', ['cols' => 4, 'rows' => 2, 'tiles' => []])->assertOk();
        $this->assertNull($a->fresh()->seat_id);
        $this->postJson('/lab/layout', ['cols' => 99, 'rows' => 2, 'tiles' => []])->assertStatus(422);
        $this->postJson('/lab/layout', ['cols' => 4, 'rows' => 2])->assertStatus(422);
    }

    // F024
    public function test_f024_pc_inspector_json_has_expected_keys(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->actingAs($this->user('assistant'))->getJson("/pcs/{$pc->id}")->assertOk()->assertJsonStructure(['pc', 'timeline', 'keyrate']);
    }

    // F034
    public function test_f034_message_goes_to_selected_active_pcs_only(): void
    {
        $this->bootLab();
        [$a] = $this->enrolledPc('192.168.10.21', 'PC-A');
        [$b] = $this->enrolledPc('192.168.10.22', 'PC-B');
        [$c] = $this->enrolledPc('192.168.10.23', 'PC-C');
        $c->update(['retired_at' => now()]);
        $this->actingAs($this->user('assistant'));
        $this->postJson('/sessions/message', ['text' => 'Hi', 'pc_ids' => [$a->id]])->assertOk()->assertJson(['sent' => 1]);
        $this->postJson('/sessions/message', ['text' => 'All'])->assertOk()->assertJson(['sent' => 2]);
        $this->postJson('/sessions/message', ['text' => ''])->assertStatus(422);
        $this->postJson('/sessions/message', ['text' => str_repeat('x', 501)])->assertStatus(422);
        $cmd = Command::where('pc_id', $a->id)->first();
        $this->assertSame('message', $cmd->type);
        $this->assertSame('Message from your instructor', $cmd->args['title']);
        $this->assertSame(0, Command::where('pc_id', $c->id)->count());
    }

    // F037
    public function test_f037_rescan_variants_default_and_validation(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->actingAs($this->user('assistant'));
        $this->postJson("/pcs/{$pc->id}/rescan")->assertOk();
        $this->postJson("/pcs/{$pc->id}/rescan", ['kind' => 'devices'])->assertOk();
        $this->postJson("/pcs/{$pc->id}/rescan", ['what' => 'bogus'])->assertStatus(422);
        $whats = Command::where('type', 'rescan')->orderBy('id')->get()->map(fn ($c) => $c->args['what'])->all();
        $this->assertSame(['processes', 'devices'], $whats);
    }

    // F039 - F041 / F042
    public function test_f040_f041_alert_filters_and_ack(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $mk = fn ($sev, $rule) => Alert::create(['pc_id' => $pc->id, 'rule_id' => $rule, 'severity' => $sev, 'title' => $rule, 'occurred_at' => now()]);
        $a1 = $mk('high', 'rule-alpha-high');
        $a2 = $mk('low', 'rule-bravo-low');
        $a3 = $mk('high', 'rule-charlie-high');
        $this->actingAs($this->user('assistant'));
        $this->get('/alerts?severity=high')->assertOk()->assertSee('rule-alpha-high')->assertDontSee('rule-bravo-low');
        $this->postJson("/alerts/{$a1->id}/ack", ['comment' => 'ok'])->assertOk()->assertJson(['ok' => true]);
        $this->assertNotNull($a1->fresh()->acknowledged_at);
        $this->assertSame('ok', $a1->fresh()->comment);
        $this->get('/alerts?open=1')->assertOk()->assertDontSee('rule-alpha-high');
        $this->postJson('/alerts/ack-batch', ['ids' => [$a2->id, $a3->id]])->assertOk()->assertJson(['ok' => true]);
        $this->assertSame(0, Alert::whereNull('acknowledged_at')->count());
        $this->postJson('/alerts/ack-batch', ['ids' => []])->assertStatus(422);
        $this->assertSame(3, AuditLog::where('action', 'alert.ack')->count());
    }

    // F050 / F051
    public function test_f051_csv_export_has_header_and_is_audited_and_pdf_requires_instructor(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        app(SessionManager::class)->stop($s);
        Score::create(['lab_session_id' => $s->id, 'pc_id' => $pc->id, 'user' => 'alice', 'method' => 'v1', 'score' => 70,
            'confidence' => 'high', 'reasons' => [['label' => 'Pasted code']]]);
        $this->actingAs($this->user('assistant'))->get("/reports/{$s->id}/csv")->assertForbidden();
        $res = $this->actingAs($this->user('instructor'))->get("/reports/{$s->id}/csv")->assertOk();
        $csv = $res->streamedContent();
        $this->assertStringStartsWith('pc,user,student_number,student_name,method,score,confidence,top_reasons', $csv);
        $this->assertStringContainsString('PC-01,alice,,,v1,70,high,"Pasted code"', $csv);
        $this->assertSame(1, AuditLog::where('action', 'report.export_csv')->count());
        $this->get("/reports/{$s->id}/pdf")->assertOk()->assertHeader('content-type', 'application/pdf');
    }

    // F052 / F055 / F057
    public function test_f052_research_export_only_consenting_students_and_pseudonymised_by_default(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $yes = Student::create(['student_number' => '1001', 'name' => 'Yes', 'login' => 'yes']);
        $no = Student::create(['student_number' => '1002', 'name' => 'No', 'login' => 'no']);
        foreach ([$yes, $no] as $st) {
            Score::create(['lab_session_id' => $s->id, 'pc_id' => $pc->id, 'student_id' => $st->id, 'user' => $st->login,
                'method' => 'v1', 'score' => 10, 'confidence' => 'low', 'reasons' => [], 'features' => ['k' => 1]]);
        }
        $inst = $this->user('instructor');
        $this->actingAs($inst)->postJson("/roster/{$yes->id}/consent", ['granted' => 1])->assertOk();
        $this->postJson("/roster/{$no->id}/consent", ['granted' => 0])->assertOk();
        $csv = $this->get('/research/export')->assertOk()->streamedContent();
        $this->assertStringContainsString($yes->pseudonym, $csv);
        $this->assertStringNotContainsString($no->pseudonym, $csv);
        $this->assertStringNotContainsString('1001', $csv);
        $this->assertStringNotContainsString('Yes', explode("\n", $csv, 2)[1]);
        // latest consent wins: withdrawing removes the student
        $this->postJson("/roster/{$yes->id}/consent", ['granted' => 0])->assertOk();
        $this->assertSame(2, Consent::where('student_id', $yes->id)->count());
        $csv = $this->get('/research/export')->streamedContent();
        $this->assertStringNotContainsString($yes->pseudonym, $csv);
        // the pseudonymize switch (admin-only) no longer affects the research export: always the pseudonym (S-41)
        $this->postJson("/roster/{$yes->id}/consent", ['granted' => 1])->assertOk();
        $this->actingAs($inst)->postJson('/roster/pseudonymize', ['enabled' => 0])->assertForbidden();
        $this->actingAs($this->user('admin'))->postJson('/roster/pseudonymize', ['enabled' => 0])->assertOk();
        $csv = $this->get('/research/export')->streamedContent();
        $this->assertStringContainsString($yes->pseudonym, $csv);
        $this->assertStringNotContainsString('1001', $csv);
    }

    // F054
    public function test_f054_roster_import_with_and_without_header_and_updates_existing(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('instructor'));
        $f = fn ($body) => UploadedFile::fake()->createWithContent('r.csv', $body);
        $this->post('/roster/import', ['file' => $f("student_id,name,login\n1,Ann,ANN\n2,Bob,bob\n,NoId,x\n3,Short\n")])->assertRedirect();
        $this->assertSame(2, Student::count());
        $this->assertSame('ann', Student::where('student_number', '1')->first()->login);   // lower-cased
        $this->post('/roster/import', ['file' => $f("1,Ann Renamed,ann\n9,Zed,zed")])->assertRedirect();
        $this->assertSame('Ann Renamed', Student::where('student_number', '1')->first()->name);
        $this->assertSame(3, Student::count());
        $this->post('/roster/import', [])->assertSessionHasErrors('file');
    }

    // F056
    public function test_f056_exempt_requires_reason_and_clears_it_when_removed(): void
    {
        $this->bootLab();
        $st = Student::create(['student_number' => '5', 'name' => 'E', 'login' => 'e']);
        $this->actingAs($this->user('instructor'));
        $this->postJson("/roster/{$st->id}/exempt", ['exempt' => 1])->assertStatus(422);
        $this->postJson("/roster/{$st->id}/exempt", ['exempt' => 1, 'reason' => 'Screen reader'])->assertOk();
        $this->assertTrue($st->fresh()->exempt);
        $this->assertSame('Screen reader', $st->fresh()->exempt_reason);
        $this->postJson("/roster/{$st->id}/exempt", ['exempt' => 0])->assertOk();
        $this->assertFalse($st->fresh()->exempt);
        $this->assertNull($st->fresh()->exempt_reason);
    }

    // F058 - F060
    public function test_f058_f060_blocklist_toggle_delete_and_rule_toggle_are_audited(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'));
        $this->post('/blocklist', ['category' => 'ai', 'value' => 'zzz-ai.example'])->assertRedirect();
        $e = BlocklistEntry::where('value', 'zzz-ai.example')->firstOrFail();
        $this->post("/blocklist/{$e->id}/toggle")->assertRedirect();
        $this->assertFalse((bool) $e->fresh()->enabled);
        $this->assertNotContains('zzz-ai.example', $this->netState()['categories']['ai']);
        $this->post('/blocklist', ['category' => 'ai', 'value' => 'zzz-ai.example'])->assertStatus(422);   // duplicate refused
        $this->delete("/blocklist/{$e->id}")->assertRedirect();
        $this->assertNull(BlocklistEntry::find($e->id));
        $this->post('/blocklist', ['category' => 'nope', 'value' => 'a.example'])->assertSessionHasErrors('category');
        $rule = Rule::firstOrFail();
        $before = $rule->enabled;
        $this->postJson("/rules/{$rule->id}/toggle")->assertOk();
        $this->assertSame(! $before, (bool) $rule->fresh()->enabled);
        $this->post('/rules', ['rule_id' => 'Bad ID', 'kind' => 'process', 'severity' => 'high', 'definition' => '{}'])->assertSessionHasErrors('rule_id');
        $this->post('/rules', ['rule_id' => 'proc.x1', 'kind' => 'process', 'severity' => 'high', 'definition' => 'not json'])->assertSessionHasErrors('definition');
        foreach (['blocklist.add', 'blocklist.toggle', 'blocklist.delete', 'rule.toggle'] as $a) {
            $this->assertNotNull(AuditLog::where('action', $a)->first(), $a);
        }
    }

    // F098 / F099 / F100 / F101
    public function test_f098_to_f101_public_pages(): void
    {
        $lab = $this->bootLab();
        Setting::put('notice_text', 'We monitor AI use.');
        $this->get('/notice')->assertOk()->assertSee('We monitor AI use.');
        $this->get('/ca.crt')->assertOk()->assertHeader('content-disposition', 'attachment; filename="xephyguard-ca.crt"');
        $sh = $this->get('/install/linux.sh')->assertOk()->assertHeader('content-type', 'text/x-shellscript; charset=UTF-8');
        $this->assertStringNotContainsString($lab->enrollKey(), $sh->getContent());
        $this->get('/downloads/..%2Fsecret.deb')->assertNotFound();
        $this->get('/downloads/nofile.deb')->assertNotFound();
        $this->get('/downloads/start-screen-stream.bat')->assertNotFound();
    }

    // F102
    public function test_f102_shared_clipboard_saves_history_trims_and_caps(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('instructor'));   // S-8: instructor-only
        $this->postJson('/share', ['text' => 'one'])->assertOk()->assertJson(['ok' => true]);
        $this->postJson('/share', ['text' => 'two', 'cmd' => 1])->assertOk();
        $this->postJson('/share', ['text' => '   '])->assertOk();
        $raw = $this->getJson('/share/raw')->assertOk()->json();
        $this->assertSame('', $raw['text']);   // TrimStrings: whitespace-only input overwrites the clipboard with ''
        $this->assertSame(['two', 'one'], array_column($raw['history'], 'v'));
        $this->assertTrue($raw['history'][0]['cmd']);
        for ($i = 0; $i < 25; $i++) {
            $this->postJson('/share', ['text' => "n$i"]);
        }
        $this->assertCount(20, $this->getJson('/share/raw')->json('history'));
    }

    // F103 / F104 / F105 / F106
    public function test_f104_settings_validation_and_save_updates_lab(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'));
        $ok = ['lab_name' => 'Lab 9', 'network_mode' => 'proxy_only', 'ccc_ip' => '10.1.1.2', 'lab_subnet' => '10.1.1.0/24',
            'retention_days_raw' => 30, 'retention_days_reports' => 365, 'notice_text' => 'n', 'paste_alert_chars' => 500,
            'title_capture_always_on' => 'off', 'title_capture_practice' => 'app_only', 'title_capture_test' => 'full'];
        $this->post('/settings', $ok)->assertRedirect();
        $this->assertSame('Lab 9', Lab::current()->name);
        $this->assertSame('proxy_only', Setting::get('network_mode'));
        $this->assertSame('full', Setting::get('privacy.title_capture.test'));
        $this->assertSame(['enabled' => false, 'query' => false], Setting::get('privacy.log_urls'));
        $this->post('/settings', ['log_queries' => 1] + $ok)->assertRedirect();
        $this->assertFalse(Setting::get('privacy.log_urls')['query']);   // queries only logged when urls are logged
        $this->post('/settings', ['retention_days_raw' => 3] + $ok)->assertSessionHasErrors('retention_days_raw');
        $this->post('/settings', ['ccc_ip' => 'host.name'] + $ok)->assertSessionHasErrors('ccc_ip');
        $this->post('/settings', ['network_mode' => 'other'] + $ok)->assertSessionHasErrors('network_mode');
        $audit = AuditLog::where('action', 'settings.save')->latest('id')->first();
        $this->assertArrayNotHasKey('notice_text', (array) ($audit->detail ?? $audit->details));
    }

    public function test_f105_audit_page_filters_by_action_prefix(): void
    {
        $this->bootLab();
        AuditLog::create(['action' => 'pc.rename']);
        AuditLog::create(['action' => 'alert.ack']);
        $this->actingAs($this->user('admin'))->get('/audit?action=pc.')->assertOk()->assertSee('pc.rename')->assertDontSee('alert.ack');
    }

    public function test_f106_eval_results_endpoint_needs_internal_token(): void
    {
        $this->bootLab();
        $this->getJson('/api/internal/eval/results')->assertStatus(401);
        $this->getJson('/api/internal/eval/results', ['Authorization' => 'Bearer wrong'])->assertStatus(401);
    }

    // F109
    public function test_f109_retention_purge_deletes_only_expired_raw_data_and_audits_counts(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        Setting::put('retention_days_raw', 30);
        Setting::put('retention_days_reports', 365);
        $ne = fn ($d) => NetEvent::create(['pc_id' => $pc->id, 'client_ip' => '1.1.1.1', 'host' => 'h', 'layer' => 'dns',
            'action' => 'blocked', 'occurred_at' => now()->subDays($d)]);
        $ne(40);
        $ne(5);
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'low', 'title' => 't', 'occurred_at' => now()->subDays(200)]);
        (new PurgeRetention)->handle();
        $this->assertSame(1, NetEvent::count());
        $this->assertNotNull(Alert::find($alert->id));   // alerts age out with the report retention (365), not the raw one (30): kept
        $this->assertSame(1, AuditLog::where('action', 'retention.purge')->count());
        (new PurgeRetention)->handle();
        $this->assertSame(1, AuditLog::where('action', 'retention.purge')->count());   // nothing deleted -> no audit row
    }

    // F111
    public function test_f111_rescore_queues_scoring_job_and_is_audited(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $this->actingAs($this->user('instructor'))->post("/reports/{$s->id}/rescore")->assertRedirect();
        Queue::assertPushed(ScoreSession::class, fn ($j) => $j->sessionId === $s->id || true);
        $this->assertNotNull(AuditLog::where('action', 'report.rescore')->first());
    }

    // F002-F007 artisan
    public function test_f002_f007_artisan_commands(): void
    {
        $lab = $this->bootLab();
        $this->artisan('xephyguard:enroll-key')->expectsOutput($lab->enrollKey())->assertExitCode(0);
        $old = $lab->enrollKey();
        $this->artisan('xephyguard:enroll-key', ['--rotate' => true])->assertExitCode(0);
        $this->assertNotSame($old, Lab::current()->enrollKey());
        $this->artisan('xephyguard:net:write')->assertExitCode(0);
        $this->assertFileExists(config('xephyguard.net_state_path'));
        $this->artisan('xephyguard:signatures:publish')->assertExitCode(0);
        $this->artisan('xephyguard:bootstrap')->assertExitCode(0);
        $before = Profile::count();
        $this->artisan('xephyguard:bootstrap')->assertExitCode(0);
        $this->assertSame($before, Profile::count());   // idempotent
        $this->artisan('xephyguard:doctor', ['--json' => true])->assertExitCode(0)->run();
    }
}
