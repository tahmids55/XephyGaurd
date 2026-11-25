<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Lab;
use App\Models\Pc;
use App\Models\SessionPc;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class SessionChangeGuardTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_ended_session_cannot_be_switched_locked_or_overridden(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $svc = app(SessionManager::class);
        $session = $svc->start($lab, $this->profile('Practice: AI blocked'), 'CS1');
        $svc->stop($session);
        $test = $this->profile('Test: AI blocked');
        $instructor = $this->user('instructor');

        $this->actingAs($instructor)->post("/sessions/{$session->id}/profile", ['profile_id' => $test->id])->assertStatus(422);
        $this->actingAs($instructor)->post("/sessions/{$session->id}/network-lock", ['network_lock' => 'strict'])->assertStatus(422);
        $this->actingAs($instructor)->post("/sessions/{$session->id}/pcs/{$pc->id}/override", ['profile_id' => $test->id, 'reason' => 'x'])
            ->assertStatus(422);

        $s = $session->fresh();
        $this->assertSame('practice', $s->mode);
        $this->assertNotSame('strict', $s->network_lock);
        $this->assertSame(0, SessionPc::whereNotNull('override_profile_id')->count());
        $this->assertDatabaseMissing('audit_log', ['action' => 'session.switch_mode']);
        $this->assertDatabaseMissing('audit_log', ['action' => 'session.override']);
    }

    public function test_active_session_still_changes_and_foreign_pc_is_refused(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $session = app(SessionManager::class)->start($lab, $this->profile('Practice: AI blocked'), 'CS2');
        $test = $this->profile('Test: AI blocked');
        $instructor = $this->user('instructor');

        $this->actingAs($instructor)->post("/sessions/{$session->id}/profile", ['profile_id' => $test->id])->assertRedirect();
        $this->assertSame('test', $session->fresh()->mode);
        $this->actingAs($instructor)->post("/sessions/{$session->id}/pcs/{$pc->id}/override", ['profile_id' => $test->id, 'reason' => 'ok'])
            ->assertRedirect();

        $other = Lab::create(['name' => 'Other']);
        $foreign = Pc::create(['lab_id' => $other->id, 'name' => 'X-1', 'hostname' => 'x1']);
        $this->actingAs($instructor)->post("/sessions/{$session->id}/pcs/{$foreign->id}/override", ['profile_id' => $test->id, 'reason' => 'x'])
            ->assertStatus(422);
    }
}
