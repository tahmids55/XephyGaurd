<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Score;
use App\Services\ReportModel;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-38: the report data of one session comes from ReportModel (moved out of ReportController). */
class ReportModelTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_build_returns_the_session_data_scoped_to_the_session(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $sm = app(SessionManager::class);
        $s = $sm->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $sm->stop($s);
        $other = $sm->start($lab, $this->profile('Test: AI blocked'), 'CS102');
        $sm->stop($other);

        Score::create(['lab_session_id' => $s->id, 'pc_id' => $pc->id, 'user' => 'a', 'method' => 'v1', 'score' => 10, 'confidence' => 'low', 'reasons' => []]);
        Score::create(['lab_session_id' => $s->id, 'pc_id' => $pc->id, 'user' => 'b', 'method' => 'v1', 'score' => 80, 'confidence' => 'high', 'reasons' => []]);
        Score::create(['lab_session_id' => $other->id, 'pc_id' => $pc->id, 'user' => 'c', 'method' => 'v1', 'score' => 50, 'confidence' => 'low', 'reasons' => []]);
        foreach ([[299, false], [300, false], [900, true]] as [$chars, $copy]) {
            PasteEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $s->id, 'user' => 'b', 'is_copy' => $copy,
                'chars' => $chars, 'lines' => 1, 'occurred_at' => now()]);
        }
        foreach (['blocked', 'allowed', 'blocked'] as $action) {
            NetEvent::create(['pc_id' => $pc->id, 'lab_session_id' => $s->id, 'client_ip' => '192.168.10.21', 'layer' => 'dns', 'host' => 'ai.example', 'category' => 'llm',
                'action' => $action, 'occurred_at' => now()]);
        }

        $m = ReportModel::build($s);

        $this->assertSame(['session', 'scores', 'alerts', 'blocked', 'pastes', 'evidence', 'report'], array_keys($m));
        $this->assertSame(['b', 'a'], $m['scores']->pluck('user')->all());
        $this->assertSame([300], $m['pastes']->pluck('chars')->all());
        $this->assertSame(2, (int) $m['blocked']->first()->n);
        $this->assertTrue($m['session']->relationLoaded('profile'));
        $this->assertNull($m['report']);
    }
}
