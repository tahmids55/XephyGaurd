<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\EvidenceCapture;
use App\Models\Rule;
use App\Models\Student;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-40 (I-SERVER-32): the per-student report resolves logins once, not once per alert/evidence row. */
class StudentLoginMapTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_for_logins_maps_each_login_with_one_query(): void
    {
        $a = Student::create(['student_number' => '1', 'name' => 'A', 'login' => 'alice']);
        $b = Student::create(['student_number' => '2', 'name' => 'B', 'login' => 'bob']);

        DB::enableQueryLog();
        $map = Student::forLogins(['alice', 'LAB\\bob', null, '', 'nobody', 'alice']);
        $this->assertCount(1, DB::getQueryLog());

        $this->assertSame(['alice', 'LAB\\bob'], array_keys($map));
        $this->assertSame($a->id, $map['alice']->id);
        $this->assertSame($b->id, $map['LAB\\bob']->id);
        $this->assertSame([], Student::forLogins([null, '']));
        $this->assertSame($a->id, Student::forLogin('alice')->id);
        $this->assertNull(Student::forLogin('nobody'));
    }

    private function addRows(int $pcId, int $sessionId, array $users): void
    {
        $ruleId = Rule::query()->value('id');
        foreach ($users as $i => $user) {
            Alert::create(['pc_id' => $pcId, 'lab_session_id' => $sessionId, 'rule_id' => $ruleId, 'severity' => 'low', 'category' => 'x',
                'title' => "t$i", 'user' => $user, 'occurred_at' => now()]);
            EvidenceCapture::create(['pc_id' => $pcId, 'lab_session_id' => $sessionId, 'user' => $user, 'file_path' => "e$i.png",
                'sha256' => str_repeat('a', 64), 'size_bytes' => 1, 'occurred_at' => now()]);
        }
    }

    /** @return array{0: int, 1: int, 2: int, 3: int} student-login queries, all queries, alerts shown, evidence shown */
    private function get_report(int $sessionId, int $studentId): array
    {
        DB::flushQueryLog();
        DB::enableQueryLog();
        $res = $this->get("/reports/$sessionId/students/$studentId");
        $res->assertOk();
        $queries = DB::getQueryLog();
        $logins = array_filter($queries, fn ($q) => str_contains($q['query'], 'from "students"') && str_contains($q['query'], '"login"'));

        return [count($logins), count($queries), $res->viewData('alerts')->count(), $res->viewData('evidence')->count()];
    }

    public function test_student_report_queries_do_not_grow_with_rows(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $sm = app(SessionManager::class);
        $s = $sm->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $sm->stop($s);
        $alice = Student::create(['student_number' => '1', 'name' => 'Alice', 'login' => 'alice']);
        $this->actingAs($this->user());

        $this->get("/reports/{$s->id}/students/{$alice->id}")->assertOk();   // warm the settings cache
        $this->addRows($pc->id, $s->id, ['alice', 'LAB\\alice', 'other0']);
        [$fewLogin, $fewAll, $alerts, $evidence] = $this->get_report($s->id, $alice->id);
        $this->assertSame([2, 2], [$alerts, $evidence]);   // alice and LAB\alice only

        $this->addRows($pc->id, $s->id, array_map(fn ($i) => "other$i", range(1, 25)));
        [$manyLogin, $manyAll] = $this->get_report($s->id, $alice->id);

        $this->assertSame(1, $fewLogin);
        $this->assertSame(1, $manyLogin);
        $this->assertSame($fewAll, $manyAll);
    }
}
