<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\AuditLog;
use App\Models\Command;
use App\Models\EvidenceCapture;
use App\Models\Pc;
use App\Models\Setting;
use App\Models\User;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Storage;
use Illuminate\Testing\TestResponse;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F123: the agent uploads the auto screenshot with the command's single-use nonce (THREAT_MODEL #34). */
class AutoEvidenceUploadTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function jpeg(): string
    {
        $im = imagecreatetruecolor(64, 48);
        ob_start();
        imagejpeg($im, null, 70);

        return (string) ob_get_clean();
    }

    private function upload(array $headers, string $query, string $body): TestResponse
    {
        return $this->call('POST', '/api/agent/v1/evidence?'.$query, [], [], [], $this->transformHeadersToServerVars($headers), $body);
    }

    /** @return array{0: Pc, 1: array<string, string>, 2: Command, 3: Alert} */
    private function requested(string $name = 'PC-01', string $ip = '192.168.10.21'): array
    {
        [$pc, $token, $cert] = $this->enrolledPc($ip, $name);
        $pc->update(['mode' => 'test']);
        $alert = Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'high', 'title' => 'Ollama', 'occurred_at' => now()]);

        return [$pc, $this->agentHeaders($token, $cert), Command::where('pc_id', $pc->id)->where('type', 'evidence_snapshot')->sole(), $alert];
    }

    public function test_agent_uploads_once_and_staff_can_view_it_audited(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [$pc, $h, $cmd, $alert] = $this->requested();
        $img = $this->jpeg();

        $this->upload($h, 'nonce='.$cmd->args['nonce'], $img)->assertCreated();
        $cap = EvidenceCapture::sole();
        $this->assertSame([$alert->id, $cmd->id, 'auto', null], [$cap->alert_id, $cap->command_id, $cap->source_mode, $cap->captured_by]);
        $this->assertSame(hash('sha256', $img), $cap->sha256);
        $this->assertNotSame($img, Storage::disk('local')->get($cap->file_path));   // encrypted at rest
        $this->assertTrue(AuditLog::where('action', 'pc.evidence.auto')->exists());

        $this->upload($h, 'nonce='.$cmd->args['nonce'], $img)->assertStatus(409);    // single use
        $this->assertSame(1, EvidenceCapture::count());

        $this->actingAs(User::factory()->create(['role' => 'instructor']))->get('/evidence/'.$cap->id)->assertOk();
        $this->assertTrue(AuditLog::where('action', 'evidence.view')->exists());
    }

    public function test_wrong_pc_bad_or_expired_nonce_and_non_jpeg_are_refused(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [, $h, $cmd] = $this->requested();
        [, $h2] = $this->requested('PC-02', '192.168.10.22');

        $this->upload($h2, 'nonce='.$cmd->args['nonce'], $this->jpeg())->assertForbidden();   // other PC's nonce
        $this->upload($h, 'nonce=zz', $this->jpeg())->assertStatus(422);
        $this->upload($h, 'nonce='.str_repeat('a', 32), $this->jpeg())->assertForbidden();
        $this->upload($h, 'nonce='.$cmd->args['nonce'], "\x89PNG not a jpeg")->assertStatus(422);
        $this->upload([], 'nonce='.$cmd->args['nonce'], $this->jpeg())->assertUnauthorized();
        $this->assertSame(0, EvidenceCapture::count());

        [, $h3, $cmd3] = $this->requested('PC-03', '192.168.10.23');
        $this->travel(61)->seconds();
        $this->upload($h3, 'nonce='.$cmd3->args['nonce'], $this->jpeg())->assertForbidden();
        $this->assertSame(0, EvidenceCapture::count());
    }

    public function test_masked_reply_is_audited_without_an_image(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [, $h, $cmd] = $this->requested();
        $this->upload($h, 'masked=1&nonce='.$cmd->args['nonce'], '')->assertOk()->assertJson(['masked' => true]);
        $this->assertSame(0, EvidenceCapture::count());
        $this->assertTrue(AuditLog::where('action', 'pc.evidence.auto_masked')->exists());
    }

    /** Plan step 8: the agent asked its helper on an `alert` rule_hit and sent the nonce with the event. */
    private function ruleHit(array $h, int $pcId, int $seq, string $nonce): void
    {
        $ts = now()->getTimestampMs();
        $this->withHeaders($h)->postJson('/api/agent/v1/events', ['v' => 1, 'pc_id' => (string) $pcId,
            'heartbeat' => ['local_time_ms' => $ts], 'events' => [['seq' => $seq, 'ts' => $ts, 'type' => 'rule_hit', 'data' => [
                'rule_id' => 'proc.ollama', 'severity' => 'high', 'description' => 'Ollama', 'subject' => 'ollama.'.$seq,
                'actions' => ['alert'], 'evidence_nonce' => $nonce]]]])->assertOk();
    }

    public function test_local_trigger_nonce_from_rule_hit_is_recorded_not_pushed_and_accepted_once(): void
    {
        Storage::fake('local');
        $this->bootLab();
        Setting::put('evidence.auto_screenshot', true);
        [$pc, $token, $cert] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        $h = $this->agentHeaders($token, $cert);
        $nonce = str_repeat('ab', 16);

        $this->ruleHit($h, $pc->id, 1, $nonce);
        $alert = Alert::where('rule_id', 'proc.ollama')->sole();
        $cmd = Command::where('type', 'evidence_snapshot')->sole();
        $this->assertSame(['local', $alert->id, $nonce], [$cmd->status, $cmd->args['alert_id'], $cmd->args['nonce']]);

        $this->upload($h, 'nonce='.$nonce, $this->jpeg())->assertCreated();
        $this->assertSame([$alert->id, $cmd->id], [EvidenceCapture::sole()->alert_id, EvidenceCapture::sole()->command_id]);
        $this->upload($h, 'nonce='.$nonce, $this->jpeg())->assertStatus(409);

        // Inside the 30 s window the CCC records nothing, so a second local shot is refused.
        $this->ruleHit($h, $pc->id, 2, str_repeat('cd', 16));
        $this->assertSame(1, Command::where('type', 'evidence_snapshot')->count());
        $this->upload($h, 'nonce='.str_repeat('cd', 16), $this->jpeg())->assertForbidden();
    }

    public function test_local_trigger_nonce_is_ignored_when_auto_screenshot_is_off(): void
    {
        Storage::fake('local');
        $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        $h = $this->agentHeaders($token, $cert);

        $this->ruleHit($h, $pc->id, 1, str_repeat('ab', 16));
        $this->assertSame(0, Command::where('type', 'evidence_snapshot')->count());
        $this->upload($h, 'nonce='.str_repeat('ab', 16), $this->jpeg())->assertForbidden();
        $this->assertSame(0, EvidenceCapture::count());
    }
}
