<?php

namespace Tests\Feature;

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
}
