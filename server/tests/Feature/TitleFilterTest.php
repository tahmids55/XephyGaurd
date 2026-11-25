<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Event;
use App\Models\Setting;
use App\Services\Ingest\TitleFilter;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-29: titles the PC's effective title_capture does not allow are dropped on the server too. */
class TitleFilterTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function send(string $token, string $cert, int $pcId, int $seq): void
    {
        $ts = now()->getTimestampMs();
        $this->withHeaders($this->agentHeaders($token, $cert))->postJson('/api/agent/v1/events', ['v' => 1,
            'pc_id' => (string) $pcId, 'heartbeat' => ['local_time_ms' => $ts], 'events' => [
                ['seq' => $seq, 'ts' => $ts, 'type' => 'window_focus', 'data' => ['app' => 'firefox', 'title' => 'Bank login - Firefox']],
                ['seq' => $seq + 1, 'ts' => $ts, 'type' => 'window_focus', 'data' => ['app' => 'code', 'title' => 'code']],
                ['seq' => $seq + 2, 'ts' => $ts, 'type' => 'rule_hit', 'data' => ['rule_id' => 'web.ai_tab', 'severity' => 'low',
                    'description' => 'AI tab', 'subject' => 'firefox:chatgpt.com', 'actions' => ['log'],
                    'context' => ['app' => 'firefox', 'title' => 'My chat - ChatGPT', 'ai_site' => 'chatgpt.com']]],
            ]])->assertOk();
    }

    /** @return list<?string> titles of the stored window_focus events + the rule_hit context title, from $seq */
    private function titles(int $seq): array
    {
        $rows = Event::query()->where('seq', '>=', $seq)->orderBy('seq')->get();

        return [$rows[0]->data['title'] ?? null, $rows[1]->data['title'] ?? null, $rows[2]->data['context']['title'] ?? null];
    }

    public function test_titles_follow_the_effective_title_capture(): void
    {
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();

        // Always-on: off -> no titles at all, even if a modified agent sends them.
        $this->send($token, $cert, $pc->id, 1);
        $this->assertSame([null, null, null], $this->titles(1));

        // Practice: app_only -> only a title that is the app name survives.
        app(SessionManager::class)->start($lab, $this->profile('Practice: AI allowed, logged'), 'CS100');
        $this->send($token, $cert, $pc->id, 10);
        $this->assertSame([null, 'code', null], $this->titles(10));

        // full (profile and setting) -> kept.
        $p = $this->profile('Practice: AI allowed, logged');
        $p->update(['agent' => ['title_capture' => 'full'] + ($p->agent ?? [])]);
        Setting::put('privacy.title_capture.practice', 'full');
        $this->send($token, $cert, $pc->id, 20);
        $this->assertSame(['Bank login - Firefox', 'code', 'My chat - ChatGPT'], $this->titles(20));
        $this->assertSame('chatgpt.com', Event::query()->where('seq', 22)->first()->data['context']['ai_site']);
    }

    public function test_paste_file_part_needs_full_and_unknown_level_is_off(): void
    {
        $paste = ['chars' => 5, 'src' => 'code (a.cpp)', 'dst' => 'code (secret plan.txt)'];
        $this->assertSame(['chars' => 5, 'src' => 'code', 'dst' => 'code'], TitleFilter::apply('paste', $paste, 'app_only'));
        $this->assertSame($paste, TitleFilter::apply('copy', $paste, 'full'));
        $this->assertSame(['app' => 'x'], TitleFilter::apply('window_focus', ['app' => 'x', 'title' => 'x'], 'everything'));
        $this->assertSame(['app' => 'x', 'title' => 'x'], TitleFilter::apply('window_focus', ['app' => 'x', 'title' => 'x'], 'app_only'));
    }
}
