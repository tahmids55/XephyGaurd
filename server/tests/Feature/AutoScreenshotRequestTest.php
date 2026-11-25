<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Command;
use App\Models\Setting;
use App\Services\NoticeText;
use App\Services\PolicyService;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Collection;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F123: a serious alert asks its PC for one screenshot, only when enabled and rate-limited (DECISIONS #32). */
class AutoScreenshotRequestTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function alert($pc, string $sev = 'high'): Alert
    {
        return Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => $sev, 'title' => 'r', 'occurred_at' => now()]);
    }

    private function sent(): Collection
    {
        return Command::query()->where('type', 'evidence_snapshot')->get();
    }

    public function test_off_by_default_sends_nothing_and_policy_and_notice_say_nothing(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        $this->alert($pc, 'critical');
        $this->assertCount(0, $this->sent());
        $this->assertArrayNotHasKey('auto_evidence', app(PolicyService::class)->forPc($pc)['screen'] ?? []);
        $this->assertStringNotContainsString('automatically', NoticeText::text());
    }

    public function test_enabled_sends_one_command_with_nonce_per_pc_per_window_and_respects_severity(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['mode' => 'test']);
        Setting::put('evidence.auto_screenshot', true);

        $this->alert($pc, 'medium');                        // below the default min severity (high)
        $this->assertCount(0, $this->sent());
        $a = $this->alert($pc, 'high');
        $this->alert($pc, 'critical');                      // inside the 30 s window
        $cmds = $this->sent();
        $this->assertCount(1, $cmds);
        $this->assertSame($a->id, $cmds[0]->args['alert_id']);
        $this->assertMatchesRegularExpression('/^[0-9a-f]{32}$/', $cmds[0]->args['nonce']);

        $this->travel(31)->seconds();
        $this->alert($pc, 'high');
        $this->assertCount(2, $this->sent());
        $this->assertNotSame($cmds[0]->args['nonce'], $this->sent()[1]->args['nonce']);

        $this->assertStringContainsString('taken automatically when a serious alert', NoticeText::text());
        $this->assertArrayNotHasKey('auto_evidence', app(PolicyService::class)->forPc($pc)['screen'] ?? []);  // no session = Always-on
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS1');
        $this->assertTrue(app(PolicyService::class)->forPc($pc->fresh())['screen']['auto_evidence']);
    }

    public function test_never_in_always_on_and_invalid_min_severity_counts_as_high(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        Setting::put('evidence.auto_screenshot', true);
        Setting::put('evidence.min_severity', 'info');
        $pc->update(['mode' => 'always_on']);
        $this->alert($pc, 'critical');
        $this->assertCount(0, $this->sent());
        $this->assertArrayNotHasKey('auto_evidence', app(PolicyService::class)->forPc($pc)['screen'] ?? []);

        $pc->update(['mode' => 'practice']);
        $this->alert($pc, 'medium');
        $this->assertCount(0, $this->sent());
        $this->alert($pc, 'high');
        $this->assertCount(1, $this->sent());
    }
}
