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
}
