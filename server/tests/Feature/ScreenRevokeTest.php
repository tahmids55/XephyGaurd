<?php

namespace Tests\Feature;

use App\Models\LabSession;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\Client\ConnectionException;
use Illuminate\Http\Client\Request;
use Illuminate\Support\Facades\Http;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ScreenRevokeTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private const URL = 'http://127.0.0.1:9/internal/revoke';

    protected function setUp(): void
    {
        parent::setUp();
        Http::fake([self::URL => Http::response(['closed' => 1])]);
    }

    private function assertRevoked(array $selector): void
    {
        Http::assertSent(fn (Request $r) => $r->url() === self::URL && $r->method() === 'POST'
            && $r->hasHeader('Authorization', 'Bearer test-internal-token') && $r->data() === $selector);
    }

    public function test_stop_view_revokes_the_audit(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.61', 'PC-RV1');
        $auditId = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->json('audit_id');

        $this->actingAs($instructor)->postJson("/api/screen/{$auditId}/stop")->assertOk();
        $this->assertRevoked(['audit_id' => $auditId]);
    }

    public function test_logout_revokes_the_users_views(): void
    {
        $this->bootLab();
        $instructor = $this->user('instructor');

        $this->actingAs($instructor)->post('/logout');
        $this->assertRevoked(['user_id' => $instructor->id]);
    }

    public function test_session_end_revokes_its_views(): void
    {
        $lab = $this->bootLab();
        $instructor = $this->user('instructor');
        $session = LabSession::create([
            'lab_id' => $lab->id, 'profile_id' => $this->profile('Test: AI blocked')->id, 'course' => 'CS101',
            'title' => 'Lab 1', 'mode' => 'exam', 'status' => 'active', 'starts_at' => now(),
            'ends_at' => now()->addHour(), 'started_at' => now(), 'started_by' => $instructor->id,
        ]);

        app(SessionManager::class)->stop($session, $instructor);
        $this->assertRevoked(['lab_session_id' => $session->id]);
    }

    public function test_deactivating_or_demoting_a_user_revokes_their_views(): void
    {
        $this->bootLab();
        $admin = $this->user('admin');
        $instructor = $this->user('instructor');
        $form = ['name' => $instructor->name, 'email' => $instructor->email];

        $this->actingAs($admin)->postJson("/users/{$instructor->id}", $form + ['role' => 'instructor', 'active' => true])->assertOk();
        Http::assertNothingSent();

        $this->actingAs($admin)->postJson("/users/{$instructor->id}", $form + ['role' => 'assistant', 'active' => true])->assertOk();
        $this->assertRevoked(['user_id' => $instructor->id]);
    }

    public function test_unreachable_gateway_never_blocks_stop(): void
    {
        Http::fake([self::URL => fn () => throw new ConnectionException('refused')]);
        $this->bootLab();
        $instructor = $this->user('instructor');
        [$pc] = $this->enrolledPc('192.168.10.62', 'PC-RV2');
        $auditId = $this->actingAs($instructor)->postJson("/api/screen/pc/{$pc->id}/token")->json('audit_id');

        $this->actingAs($instructor)->postJson("/api/screen/{$auditId}/stop")->assertOk();
        $this->assertDatabaseMissing('screen_view_audits', ['id' => $auditId, 'ended_at' => null]);
    }
}
