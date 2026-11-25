<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Consent;
use App\Models\Score;
use App\Models\Setting;
use App\Models\Student;
use App\Services\ResearchExport;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-41 (I-SERVER-9, I-SERVER-33, I-ANALYSIS-4): one research export, pseudonym always, consent in SQL, streamed. */
class ResearchExportTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private int $pcId;

    private int $sessionId;

    protected function setUp(): void
    {
        parent::setUp();
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->pcId = $pc->id;
        $this->sessionId = app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101')->id;
    }

    public function test_only_latest_granted_research_consent_and_only_the_pseudonym(): void
    {
        Setting::put('pseudonymize_exports', false); // must not matter
        $t = now()->subDay();
        $granted = $this->student('1001', 'Granted', [[true, $t]]);
        $withdrawn = $this->student('1002', 'Withdrawn', [[true, $t], [false, $t->copy()->addHour()]]);
        $regranted = $this->student('1003', 'Regranted', [[false, $t], [true, $t->copy()->addHour()]]);
        $tieLast = $this->student('1004', 'TieGranted', [[false, $t], [true, $t]]); // same time: higher id wins
        $tieOff = $this->student('1005', 'TieWithdrawn', [[true, $t], [false, $t]]);
        $none = $this->student('1006', 'NoConsent', []);
        $other = $this->student('1007', 'OtherPurpose', []);
        Consent::create(['student_id' => $other->id, 'purpose' => 'teaching', 'granted' => true, 'recorded_at' => $t]);

        $rows = ResearchExport::rows()->all();
        $this->assertSame([$granted->pseudonym, $regranted->pseudonym, $tieLast->pseudonym], array_column($rows, 5));
        $this->assertSame([$this->sessionId, 'CS101', 'v1', 10], [$rows[0][0], $rows[0][1], $rows[0][6], $rows[0][7]]);
        $this->assertSame('{"k":1}', $rows[0][9]);
        $this->assertCount(count(ResearchExport::COLUMNS), $rows[0]);

        $csv = $this->actingAs($this->user('instructor'))->get('/research/export')->assertOk()->streamedContent();
        foreach ([$granted, $withdrawn, $regranted, $tieLast, $tieOff, $none, $other] as $s) {
            $this->assertStringNotContainsString($s->student_number, $csv);
            $this->assertStringNotContainsString($s->name, $csv);
            $this->assertStringNotContainsString($s->login, $csv);
        }
        $this->assertStringNotContainsString($withdrawn->pseudonym, $csv);
        $this->assertStringNotContainsString($tieOff->pseudonym, $csv);
        $this->assertStringStartsWith(implode(',', ResearchExport::COLUMNS)."\n", $csv);
    }

    public function test_consent_is_filtered_in_sql_and_rows_stream_in_chunks(): void
    {
        $yes = $this->student('2001', 'Yes', [[true, now()]]);
        $this->student('2002', 'No', [[false, now()]]);
        $this->assertSame([1, 1], $this->queriesAndRows()); // one query, the refused student never leaves the DB

        $more = array_map(fn (int $i) => ['lab_session_id' => $this->sessionId, 'pc_id' => $this->pcId, 'student_id' => $yes->id,
            'user' => "u{$i}", 'method' => 'v1', 'score' => 1, 'confidence' => 'low', 'reasons' => '[]', 'features' => '{}'], range(1, 500));
        DB::table('scores')->insert($more);
        $this->assertSame([2, 501], $this->queriesAndRows()); // chunks of 500 by id, not one get() of everything
    }

    /** @return array{int, int} queries issued and rows produced by one full export */
    private function queriesAndRows(): array
    {
        DB::flushQueryLog();
        DB::enableQueryLog();
        $rows = ResearchExport::rows()->count();
        $queries = count(DB::getQueryLog());
        DB::disableQueryLog();

        return [$queries, $rows];
    }

    /** @param list<array{bool, \DateTimeInterface}> $consents in insert order */
    private function student(string $number, string $name, array $consents): Student
    {
        $s = Student::create(['student_number' => $number, 'name' => $name, 'login' => 'login'.$number]);
        foreach ($consents as [$granted, $at]) {
            Consent::create(['student_id' => $s->id, 'purpose' => 'research', 'granted' => $granted, 'recorded_at' => $at]);
        }
        Score::create(['lab_session_id' => $this->sessionId, 'pc_id' => $this->pcId, 'student_id' => $s->id, 'user' => $s->login,
            'method' => 'v1', 'score' => 10, 'confidence' => 'low', 'reasons' => [], 'features' => ['k' => 1]]);

        return $s;
    }
}
