<?php

namespace Tests\Feature;

use App\Models\Command;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\ScreenViewAudit;
use App\Models\SessionPc;
use App\Models\Student;
use Firebase\JWT\JWT;
use Firebase\JWT\Key;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ViewTokenTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_unauthenticated_request_is_unauthorized(): void
    {
        $this->bootLab();
        $student = Student::create(['name' => 'Alice', 'login' => 'alice', 'student_number' => 'S1001']);

        $res = $this->postJson("/api/screen/{$student->id}/token");
        $res->assertUnauthorized();
    }

    public function test_instructor_of_another_lab_session_gets_forbidden(): void
    {
        $lab = $this->bootLab();
        $instructor1 = $this->user('instructor');
        $instructor2 = $this->user('instructor');

        [$pc] = $this->enrolledPc('192.168.10.31', 'PC-02');
        $student = Student::create(['name' => 'Bob', 'login' => 'bob', 'student_number' => 'S1002']);

        // Session started by instructor 1
        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $instructor1->id,
        ]);

        SessionPc::create([
            'lab_session_id' => $session->id,
            'pc_id' => $pc->id,
            'student_id' => $student->id,
        ]);

        // Instructor 2 attempts to get a view token for Instructor 1's student
        $res = $this->actingAs($instructor2)->postJson("/api/screen/{$student->id}/token", [
            'mode' => 'live',
        ]);

        $res->assertForbidden();
    }

    public function test_owning_instructor_gets_valid_jwt_and_audit(): void
    {
        $lab = $this->bootLab();
        $instructor = $this->user('instructor');

        [$pc] = $this->enrolledPc('192.168.10.32', 'PC-03');
        $student = Student::create(['name' => 'Charlie', 'login' => 'charlie', 'student_number' => 'S1003']);

        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $instructor->id,
        ]);

        SessionPc::create([
            'lab_session_id' => $session->id,
            'pc_id' => $pc->id,
            'student_id' => $student->id,
        ]);

        $res = $this->actingAs($instructor)->postJson("/api/screen/{$student->id}/token", [
            'mode' => 'live',
        ]);

        $res->assertOk();
        $res->assertJsonStructure([
            'token',
            'stream_id',
            'audit_id',
            'expires_at',
            'gateway_url',
            'mode',
        ]);

        $token = $res->json('token');
        $cfg = config('services.screen_gateway');

        // Decode JWT and verify claims against gateway auth specifications
        $decoded = JWT::decode($token, new Key($cfg['secret'], 'HS256'));
        $this->assertSame($cfg['issuer'], $decoded->iss);
        $this->assertSame($cfg['audience'], $decoded->aud);
        $this->assertSame((string) $instructor->id, $decoded->sub);
        $this->assertSame($student->id, $decoded->student_id);
        $this->assertSame($session->id, $decoded->lab_session_id);
        $this->assertSame($pc->id, $decoded->pc_id);
        $this->assertSame($pc->id, $decoded->stream_id);
        $this->assertSame($res->json('audit_id'), $decoded->audit_id);
        $this->assertSame('live', $decoded->mode);
        $this->assertNotEmpty($decoded->jti);
        $this->assertSame(30, $decoded->exp - $decoded->iat);

        // Verify audit log entry
        $this->assertDatabaseHas('screen_view_audits', [
            'id' => $res->json('audit_id'),
            'instructor_id' => $instructor->id,
            'student_id' => $student->id,
            'lab_session_id' => $session->id,
            'mode' => 'live',
            'ended_at' => null,
        ]);
    }

    public function test_student_without_active_session_returns_not_found(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        $student = Student::create(['name' => 'Dan', 'login' => 'dan', 'student_number' => 'S1004']);

        $res = $this->actingAs($instructor)->postJson("/api/screen/{$student->id}/token");
        $res->assertNotFound();
    }

    public function test_admin_can_issue_token_for_any_session(): void
    {
        $lab = $this->bootLab();
        $instructor = $this->user('instructor');
        $admin = $this->user('admin');

        [$pc] = $this->enrolledPc('192.168.10.33', 'PC-04');
        $student = Student::create(['name' => 'Eve', 'login' => 'eve', 'student_number' => 'S1005']);

        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $instructor->id,
        ]);

        SessionPc::create([
            'lab_session_id' => $session->id,
            'pc_id' => $pc->id,
            'student_id' => $student->id,
        ]);

        // Admin can view even if session was started by instructor
        $res = $this->actingAs($admin)->postJson("/api/screen/{$student->id}/token");
        $res->assertOk();
    }

    public function test_lease_renewal_and_stream_stop(): void
    {
        $lab = $this->bootLab();
        $instructor = $this->user('instructor');

        [$pc] = $this->enrolledPc('192.168.10.34', 'PC-05');
        $student = Student::create(['name' => 'Frank', 'login' => 'frank', 'student_number' => 'S1006']);

        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $instructor->id,
        ]);

        SessionPc::create([
            'lab_session_id' => $session->id,
            'pc_id' => $pc->id,
            'student_id' => $student->id,
        ]);

        // Issue initial token
        $res = $this->actingAs($instructor)->postJson("/api/screen/{$student->id}/token");
        $auditId = $res->json('audit_id');
        $audit = ScreenViewAudit::findOrFail($auditId);

        // Renew lease
        $renewRes = $this->actingAs($instructor)->postJson("/api/screen/{$audit->id}/renew");
        $renewRes->assertOk();
        $renewRes->assertJsonStructure(['token', 'expires_at']);

        // Stop stream
        $stopRes = $this->actingAs($instructor)->postJson("/api/screen/{$audit->id}/stop");
        $stopRes->assertOk();
        $stopRes->assertJson(['status' => 'stopped']);

        $audit->refresh();
        $this->assertNotNull($audit->ended_at);

        // Renewing an ended stream should fail
        $renewEnded = $this->actingAs($instructor)->postJson("/api/screen/{$audit->id}/renew");
        $renewEnded->assertStatus(422);
    }

    public function test_stop_reports_a_positive_duration(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.35', 'PC-06');

        $this->travelTo(now()->startOfSecond());
        $audit = ScreenViewAudit::create([
            'instructor_id' => $instructor->id,
            'pc_id' => $pc->id,
            'started_at' => now(),
        ]);

        $this->travel(90)->seconds();
        $res = $this->actingAs($instructor)->postJson("/api/screen/{$audit->id}/stop");

        $res->assertOk();
        $this->assertSame(90, $res->json('duration_s'));
    }

    public function test_issue_token_directly_for_pc_without_session(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.40', 'PC-DIRECT');

        $res = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token", [
            'mode' => 'live',
        ]);

        $res->assertOk();
        $res->assertJsonStructure(['token', 'stream_id', 'audit_id', 'gateway_url', 'mode']);
        $this->assertEquals($pc->id, $res->json('stream_id'));

        $audit = ScreenViewAudit::findOrFail($res->json('audit_id'));
        $this->assertEquals($pc->id, $audit->pc_id);
        $this->assertNull($audit->student_id);
        $this->assertNull($audit->lab_session_id);
    }

    public function test_pc_token_during_active_session_names_the_student(): void
    {
        $lab = $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.44', 'PC-INSESSION');
        $student = Student::create(['name' => 'Ivy', 'login' => 'ivy', 'student_number' => 'S1009']);
        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $instructor->id,
        ]);
        SessionPc::create(['lab_session_id' => $session->id, 'pc_id' => $pc->id, 'student_id' => $student->id]);

        $res = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token");
        $res->assertOk();
        $this->assertDatabaseHas('screen_view_audits', [
            'id' => $res->json('audit_id'), 'pc_id' => $pc->id, 'student_id' => $student->id, 'lab_session_id' => $session->id,
        ]);
    }

    private function activeSessionFor(int $startedBy, Student $student, int $pcId): LabSession
    {
        $session = LabSession::create([
            'lab_id' => Lab::first()->id,
            'profile_id' => $this->profile('Test: AI blocked')->id,
            'course' => 'CS101',
            'title' => 'Lab 1',
            'mode' => 'exam',
            'status' => 'active',
            'starts_at' => now(),
            'ends_at' => now()->addHours(2),
            'started_at' => now(),
            'started_by' => $startedBy,
        ]);
        SessionPc::create(['lab_session_id' => $session->id, 'pc_id' => $pcId, 'student_id' => $student->id]);

        return $session;
    }

    public function test_client_stream_id_is_ignored_for_student_and_pc_tokens(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.41', 'PC-OWN');
        [$other] = $this->enrolledPc('192.168.10.42', 'PC-OTHER');
        $student = Student::create(['name' => 'Gina', 'login' => 'gina', 'student_number' => 'S1007']);
        $this->activeSessionFor($instructor->id, $student, $pc->id);
        $secret = config('services.screen_gateway.secret');

        $res = $this->actingAs($instructor)->postJson("/api/screen/{$student->id}/token", ['stream_id' => $other->id]);
        $res->assertOk();
        $this->assertSame($pc->id, $res->json('stream_id'));
        $claims = JWT::decode($res->json('token'), new Key($secret, 'HS256'));
        $this->assertSame($pc->id, $claims->stream_id);
        $this->assertSame($pc->id, $claims->pc_id);

        $res = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token", ['stream_id' => $other->id]);
        $res->assertOk();
        $this->assertSame($pc->id, $res->json('stream_id'));
        $claims = JWT::decode($res->json('token'), new Key($secret, 'HS256'));
        $this->assertSame($pc->id, $claims->stream_id);
        $this->assertSame($pc->id, $claims->pc_id);
        $this->assertDatabaseHas('screen_view_audits', ['id' => $res->json('audit_id'), 'stream_id' => $pc->id]);
    }

    public function test_renewed_token_keeps_pc_id(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.43', 'PC-RENEW');
        $student = Student::create(['name' => 'Hal', 'login' => 'hal', 'student_number' => 'S1008']);
        $this->activeSessionFor($instructor->id, $student, $pc->id);

        $auditId = $this->actingAs($instructor)->postJson("/api/screen/{$student->id}/token")->json('audit_id');
        $this->assertDatabaseHas('screen_view_audits', ['id' => $auditId, 'pc_id' => $pc->id, 'stream_id' => $pc->id]);

        $renew = $this->actingAs($instructor)->postJson("/api/screen/{$auditId}/renew");
        $renew->assertOk();
        $claims = JWT::decode($renew->json('token'), new Key(config('services.screen_gateway.secret'), 'HS256'));
        $this->assertSame($pc->id, $claims->pc_id);
        $this->assertSame($pc->id, $claims->stream_id);
        $this->assertSame($auditId, $claims->audit_id);
        $this->assertSame((string) $instructor->id, $claims->sub);
    }

    public function test_admin_renewal_keeps_the_viewer_as_sub(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        $admin = $this->user('admin');
        [$pc] = $this->enrolledPc('192.168.10.47', 'PC-ADMRENEW');

        $auditId = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->json('audit_id');
        $renew = $this->actingAs($admin)->postJson("/api/screen/{$auditId}/renew")->assertOk();
        $claims = JWT::decode($renew->json('token'), new Key(config('services.screen_gateway.secret'), 'HS256'));
        $this->assertSame((string) $instructor->id, $claims->sub);
    }

    public function test_missing_secret_writes_no_audit(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.48', 'PC-NOSECRET');
        config(['services.screen_gateway.secret' => '']);

        $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->assertStatus(500);
        $this->assertDatabaseCount('screen_view_audits', 0);
    }

    public function test_audit_without_pc_cannot_be_renewed(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        $audit = ScreenViewAudit::create([
            'instructor_id' => $instructor->id,
            'stream_id' => 5001,
            'mode' => 'live',
            'started_at' => now(),
        ]);

        $this->actingAs($instructor)->postJson("/api/screen/{$audit->id}/renew")->assertStatus(422);
    }

    public function test_assistant_cannot_get_screen_tokens(): void
    {
        $this->bootLab();
        $assistant = $this->user('assistant');
        [$pc] = $this->enrolledPc('192.168.10.45', 'PC-ASSIST');
        $student = Student::create(['name' => 'Jo', 'login' => 'jo', 'student_number' => 'S1010']);
        $this->activeSessionFor($assistant->id, $student, $pc->id);

        $this->actingAs($assistant)->postJson("/api/screen/pc/{$pc->id}/token")->assertForbidden();
        $this->actingAs($assistant)->postJson("/api/screen/{$student->id}/token")->assertForbidden();
        $this->assertDatabaseCount('screen_view_audits', 0);
    }

    public function test_deactivated_instructor_cannot_view_or_renew(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.46', 'PC-INACTIVE');

        $auditId = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->assertOk()->json('audit_id');
        $instructor->update(['active' => false]);

        $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->assertForbidden();
        $this->actingAs($instructor)->postJson("/api/screen/{$auditId}/renew")->assertForbidden();
    }

    public function test_every_token_and_renewal_sends_the_pc_a_screen_lease(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.44', 'PC-LEASE');
        [$other] = $this->enrolledPc('192.168.10.45', 'PC-OTHER');

        $res = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token", ['mode' => 'live']);
        $res->assertOk();
        $this->actingAs($instructor)->postJson('/api/screen/'.$res->json('audit_id').'/renew')->assertOk();

        $leases = Command::where('type', 'screen_lease')->get();
        $this->assertCount(2, $leases);
        foreach ($leases as $c) {
            $this->assertSame($pc->id, $c->pc_id);
            $this->assertSame(['ttl_s' => (int) config('services.screen_gateway.ttl', 30)], $c->args);
        }
        $this->assertSame(0, Command::where('type', 'screen_lease')->where('pc_id', $other->id)->count());
    }
}
