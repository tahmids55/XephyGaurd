<?php

namespace Tests\Feature;

// Characterization tests: pin CURRENT behavior (docs/features/server: profiles, timetable, policy). Not a spec.

use App\Models\AuditLog;
use App\Models\LabSession;
use App\Models\Profile;
use App\Models\Setting;
use App\Models\TimetableEntry;
use App\Services\PolicyService;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class FeatureSessionsTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    // F030
    public function test_f030_five_builtin_profiles_are_seeded_and_cannot_be_deleted(): void
    {
        $this->bootLab();
        $this->assertSame(5, Profile::where('builtin', true)->count());
        $this->assertEqualsCanonicalizing(['Always-on', 'Practice: AI allowed, logged', 'Practice: AI blocked', 'Test: AI blocked', 'Exam: strict'],
            Profile::where('builtin', true)->pluck('name')->all());
        $this->actingAs($this->user('admin'))->delete('/profiles/'.$this->profile('Exam: strict')->id)->assertStatus(422);
        $this->assertNotNull($this->profile('Exam: strict'));
    }

    // F029
    public function test_f029_custom_profile_crud_validation_and_audit(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'));
        $body = ['name' => 'Quiz', 'mode' => 'test', 'title_capture' => 'full', 'block_categories' => ['ai'],
            'collect' => ['window', 'keys'], 'kill_rules' => 'proc.ollama, proc.llamacpp'];
        $this->post('/profiles', $body)->assertRedirect();
        $p = $this->profile('Quiz');
        $this->assertFalse((bool) $p->builtin);
        $this->assertSame(['ai'], $p->network['block_categories']);
        $this->assertSame(['window' => true, 'clipboard' => false, 'keys' => true, 'files' => false, 'gpu' => false, 'artifacts' => false], $p->agent['collect']);
        $this->assertSame(['log', 'warn', 'kill', 'alert'], $p->agent['rule_actions']['proc.ollama']);
        $this->post('/profiles', $body)->assertSessionHasErrors('name');   // unique
        $this->post("/profiles/{$p->id}", ['name' => 'Quiz 2'] + $body)->assertRedirect();
        $this->assertSame('Quiz 2', $p->fresh()->name);
        $this->post('/profiles', ['mode' => 'bogus', 'name' => 'x', 'title_capture' => 'off'])->assertSessionHasErrors('mode');
        $this->post('/profiles', ['name' => 'y', 'mode' => 'test', 'title_capture' => 'off', 'block_categories' => ['nope']])->assertSessionHasErrors('block_categories.0');
        $this->actingAs($this->user('instructor'))->post('/profiles', $body)->assertForbidden();
        $this->actingAs($this->user('admin'))->delete("/profiles/{$p->id}")->assertRedirect();
        $this->assertNull(Profile::find($p->id));
        $this->assertSame(1, AuditLog::where('action', 'profile.delete')->count());
    }

    // F031
    public function test_f031_timetable_crud_validation_and_profile_in_use_is_protected(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('instructor'));
        $custom = Profile::create(['name' => 'T1', 'mode' => 'test', 'network' => [], 'agent' => []]);
        $body = ['course' => 'CS1', 'profile_id' => $custom->id, 'weekday' => 2, 'start_time' => '09:00', 'end_time' => '10:30'];
        $this->post('/timetable', $body)->assertRedirect();
        $e = TimetableEntry::firstOrFail();
        $this->assertTrue((bool) $e->enabled);
        $this->post('/timetable', ['end_time' => '08:00'] + $body)->assertSessionHasErrors('end_time');
        $this->post('/timetable', ['weekday' => 8] + $body)->assertSessionHasErrors('weekday');
        $this->post('/timetable', ['start_time' => '9am'] + $body)->assertSessionHasErrors('start_time');
        $this->post('/timetable', ['valid_from' => '2026-02-01', 'valid_until' => '2026-01-01'] + $body)->assertSessionHasErrors('valid_until');
        $this->postJson("/timetable/{$e->id}/toggle")->assertOk();
        $this->assertFalse((bool) $e->fresh()->enabled);
        $this->actingAs($this->user('admin'))->delete("/profiles/{$custom->id}")->assertStatus(422);   // used by the timetable
        $this->actingAs($this->user('instructor'))->deleteJson("/timetable/{$e->id}")->assertOk();
        $this->assertSame(0, TimetableEntry::count());
        $this->actingAs($this->user('assistant'))->post('/timetable', $body)->assertForbidden();
    }

    // F031 overlap behavior
    public function test_f031_overlapping_entries_are_accepted_and_first_covering_entry_starts(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        $this->actingAs($this->user('instructor'));
        $a = Profile::create(['name' => 'TA', 'mode' => 'test', 'network' => [], 'agent' => []]);
        $b = Profile::create(['name' => 'TB', 'mode' => 'practice', 'network' => [], 'agent' => []]);
        foreach ([[$a, 'CS-A'], [$b, 'CS-B']] as [$p, $course]) {
            $this->post('/timetable', ['course' => $course, 'profile_id' => $p->id, 'weekday' => 3, 'start_time' => '09:00', 'end_time' => '11:00'])->assertRedirect();
        }
        $this->assertSame(2, TimetableEntry::count());
        $wed = now()->startOfWeek()->addDays(2)->setTime(9, 30);
        [$started] = app(SessionManager::class)->timetableTick($wed);
        $this->assertSame(1, $started);
        $this->assertSame(1, LabSession::count());
    }

    // F028
    public function test_f028_per_pc_override_beats_session_profile_in_policy_and_is_audited(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $s = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS1');
        $this->actingAs($this->user('instructor'));
        $this->post("/sessions/{$s->id}/pcs/{$pc->id}/override", ['profile_id' => $this->profile('Practice: AI allowed, logged')->id])->assertSessionHasErrors('reason');
        $this->post("/sessions/{$s->id}/pcs/{$pc->id}/override", ['profile_id' => $this->profile('Practice: AI allowed, logged')->id, 'reason' => 'Accommodation'])->assertRedirect();
        $this->assertSame('practice', app(PolicyService::class)->forPc($pc)['mode']);
        $this->assertSame('Accommodation', AuditLog::where('action', 'session.override')->first()->detail['reason'] ?? AuditLog::where('action', 'session.override')->first()->details['reason']);
        $this->post("/sessions/{$s->id}/pcs/{$pc->id}/override", ['reason' => 'cleared'])->assertRedirect();   // no profile = remove
        $this->assertSame('test', app(PolicyService::class)->forPc($pc)['mode']);
        $this->actingAs($this->user('assistant'))->post("/sessions/{$s->id}/pcs/{$pc->id}/override", ['reason' => 'x'])->assertForbidden();
    }

    // F032 / F033
    public function test_f032_f033_policy_payload_carries_notice_title_capture_and_is_deterministic(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        Setting::put('notice_text', 'Be fair.');
        $pol = app(PolicyService::class)->forPc($pc);
        $this->assertSame('always_on', $pol['mode']);
        $this->assertSame(0, $pol['session_id']);
        $this->assertStringStartsWith('Be fair. ', $pol['notice_text']);   // + generated screen sentences (S-25)
        $this->assertStringContainsString('view this screen live', $pol['notice_text']);
        // Fixed in S-20 (was a known bug): no saved setting = the safe always_on default 'off'.
        $this->assertSame('off', $pol['title_capture']);
        $this->assertSame($pol['version'], app(PolicyService::class)->forPc($pc)['version']);
        Setting::put('notice_text', 'Changed.');
        $this->assertNotSame($pol['version'], app(PolicyService::class)->forPc($pc)['version']);
        $this->assertArrayNotHasKey('proxy_pac_url', $pol);   // gateway mode: no PAC
        Setting::put('network_mode', 'proxy_only');
        Setting::put('ccc_ip', '10.1.1.2');
        $this->assertSame('https://10.1.1.2/proxy.pac', app(PolicyService::class)->forPc($pc)['proxy_pac_url']);
    }

    // F027 session lifecycle: start twice, stop twice
    public function test_f027_session_start_validation_and_stop_is_idempotent(): void
    {
        Queue::fake();
        $this->bootLab();
        $this->actingAs($this->user('assistant'));
        $pid = $this->profile('Practice: AI allowed, logged')->id;
        $this->post('/sessions', ['course' => 'C', 'profile_id' => $pid, 'duration_min' => 1])->assertSessionHasErrors('duration_min');
        $this->post('/sessions', ['course' => 'C', 'profile_id' => 999])->assertSessionHasErrors('profile_id');
        $this->post('/sessions', ['course' => 'C', 'profile_id' => $pid, 'duration_min' => 60, 'experiment_condition' => 'treatment'])->assertRedirect();
        $s = LabSession::firstOrFail();
        $this->assertSame('active', $s->status);
        $this->assertNotNull($s->ends_at);
        $this->assertSame('treatment', $s->experiment_condition);
        $this->post("/sessions/{$s->id}/stop")->assertRedirect();
        $this->post("/sessions/{$s->id}/stop")->assertRedirect();
        $this->assertSame('ended', $s->fresh()->status);
    }

    // F035 / F036 command side
    public function test_f035_kill_requires_exe_or_pid_and_f036_isolate_toggles_flag(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->actingAs($this->user('assistant'));
        $this->postJson("/pcs/{$pc->id}/kill", [])->assertStatus(422);
        $this->postJson("/pcs/{$pc->id}/kill", ['exe' => 'cursor'])->assertOk()->assertJsonStructure(['command_id']);
        $this->processSnapshot($pc, [['pid' => 77, 'exe' => 'ollama', 'start' => 5]]);   // S-30: pid must be running
        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 77])->assertOk();
        $this->postJson("/pcs/{$pc->id}/isolate", ['isolated' => true, 'reason' => 'exam'])->assertOk();
        $this->assertTrue((bool) $pc->fresh()->isolated);
        $this->postJson("/pcs/{$pc->id}/isolate", ['isolated' => false])->assertOk();
        $this->assertFalse((bool) $pc->fresh()->isolated);
        $this->assertSame(['pc.isolate', 'pc.unisolate'], AuditLog::whereIn('action', ['pc.isolate', 'pc.unisolate'])->orderBy('id')->pluck('action')->all());
        $this->postJson("/pcs/{$pc->id}/isolate", [])->assertStatus(422);
    }
}
