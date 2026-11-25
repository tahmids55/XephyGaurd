<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Agent;
use App\Models\Alert;
use App\Models\AuditLog;
use App\Models\Command;
use App\Models\Event;
use App\Models\EvidenceCapture;
use App\Models\Setting;
use App\Models\User;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Storage;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F123 Step 7: alert detail shows the frozen context (audited) and the auto screenshot (instructor, audited). */
class AlertEvidenceViewTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function jpeg(): string
    {
        ob_start();
        imagejpeg(imagecreatetruecolor(32, 24), null, 70);

        return (string) ob_get_clean();
    }

    public function test_context_is_shown_on_request_and_every_view_is_audited(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['active_app' => 'firefox']);
        $this->processSnapshot($pc, [['pid' => 2, 'name' => 'ollama'], ['pid' => 1, 'name' => 'bash'], ['pid' => 3, 'name' => 'ollama']], 1);
        Event::create(['pc_id' => $pc->id, 'agent_id' => Agent::where('pc_id', $pc->id)->value('id'), 'seq' => 2,
            'type' => 'window_focus', 'occurred_at' => now()->subSeconds(5), 'received_at' => now(),
            'data' => ['app' => 'firefox', 'title' => 'ChatGPT', 'nested' => ['x' => 1]]]);
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'high', 'title' => 'Ollama', 'occurred_at' => now()]);
        $assistant = User::factory()->create(['role' => 'assistant']);
        $this->getJson('/alerts/'.$alert->id.'/snapshot')->assertUnauthorized();

        $this->actingAs($assistant)->getJson('/alerts/'.$alert->id)->assertOk()
            ->assertJson(['has_snapshot' => true, 'screenshot' => null]);
        $this->assertFalse(AuditLog::where('action', 'alert.snapshot.view')->exists());   // detail alone: no context read

        $this->actingAs($assistant)->getJson('/alerts/'.$alert->id.'/snapshot')->assertOk()
            ->assertJson(['intact' => true, 'pc' => ['active_app' => 'firefox'],
                'processes' => ['count' => 3, 'names' => ['bash', 'ollama']],
                'events' => [['type' => 'window_focus', 'summary' => 'app=firefox · title=ChatGPT']]]);
        $this->assertSame(1, AuditLog::where('action', 'alert.snapshot.view')->where('subject_id', $alert->id)->count());

        $alert->snapshot->update(['payload' => str_replace('firefox', 'edited', $alert->snapshot->payload)]);
        $this->actingAs($assistant)->getJson('/alerts/'.$alert->id.'/snapshot')->assertOk()->assertJson(['intact' => false]);
        $this->assertSame(2, AuditLog::where('action', 'alert.snapshot.view')->count());

        $alert->snapshot->delete();
        $this->actingAs($assistant)->getJson('/alerts/'.$alert->id.'/snapshot')->assertNotFound();
    }

    public function test_screenshot_link_only_for_instructors_and_viewing_is_audited(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [$pc, $token, $cert] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'high', 'title' => 'Ollama', 'occurred_at' => now()]);
        $cmd = Command::where('pc_id', $pc->id)->where('type', 'evidence_snapshot')->sole();
        $instructor = User::factory()->create(['role' => 'instructor']);
        $assistant = User::factory()->create(['role' => 'assistant']);

        $this->actingAs($instructor)->getJson('/alerts/'.$alert->id)->assertJsonPath('screenshot.status', 'requested');

        $img = $this->jpeg();
        $this->call('POST', '/api/agent/v1/evidence?nonce='.$cmd->args['nonce'], [], [], [],
            $this->transformHeadersToServerVars($this->agentHeaders($token, $cert)), $img)->assertCreated();
        $cap = EvidenceCapture::sole();

        $this->actingAs($assistant)->getJson('/alerts/'.$alert->id)
            ->assertJsonPath('screenshot.status', 'captured')->assertJsonPath('screenshot.url', null)
            ->assertJsonPath('screenshot.can_view', false);
        $this->actingAs($assistant)->get('/evidence/'.$cap->id)->assertForbidden();

        $url = $this->actingAs($instructor)->getJson('/alerts/'.$alert->id)
            ->assertJsonPath('screenshot.sha256', hash('sha256', $img))->json('screenshot.url');
        $this->assertSame(route('evidence.show', $cap), $url);
        $this->assertFalse(AuditLog::where('action', 'evidence.view')->exists());   // listing is not viewing
        $this->actingAs($instructor)->get($url)->assertOk();
        $view = AuditLog::where('action', 'evidence.view')->sole();
        $this->assertSame($alert->id, $view->details['alert_id']);
    }

    public function test_masked_reply_shows_as_masked(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [$pc, $token, $cert] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'high', 'title' => 'Ollama', 'occurred_at' => now()]);
        $cmd = Command::where('pc_id', $pc->id)->where('type', 'evidence_snapshot')->sole();
        $this->call('POST', '/api/agent/v1/evidence?masked=1&nonce='.$cmd->args['nonce'], [], [], [],
            $this->transformHeadersToServerVars($this->agentHeaders($token, $cert)), '')->assertOk();

        $this->actingAs(User::factory()->create(['role' => 'instructor']))->getJson('/alerts/'.$alert->id)
            ->assertJsonPath('screenshot.status', 'masked');
    }
}
