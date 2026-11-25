<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Score;
use App\Services\ReportModel;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-39 (I-SERVER-20): HTML, PDF and CSV show the same score and reasons as stored, plus not-observable. */
class ReportRenderTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function makeSession(): array
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $sm = app(SessionManager::class);
        $s = $sm->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $sm->stop($s);
        Score::create(['lab_session_id' => $s->id, 'pc_id' => $pc->id, 'user' => 'alice', 'method' => 'v1', 'score' => 73,
            'confidence' => 'high',
            'reasons' => [['points' => 31, 'label' => 'Pasted code', 'signal' => 'paste.large'], ['label' => 'Odd hours']],
            'not_observable' => ['Phones and paper notes']]);

        return [$s];
    }

    public function test_score_rows_keep_stored_values_and_null_points(): void
    {
        [$s] = $this->makeSession();

        $r = ReportModel::scoreRows(ReportModel::scores($s))[0];

        $this->assertSame(73, $r['score']);
        $this->assertSame([['points' => 31, 'label' => 'Pasted code', 'signal' => 'paste.large'],
            ['points' => null, 'label' => 'Odd hours', 'signal' => '']], $r['reasons']);
        $this->assertSame(['Phones and paper notes'], $r['not_observable']);
    }

    public function test_html_pdf_model_and_csv_agree(): void
    {
        [$s] = $this->makeSession();
        $u = $this->user('instructor');

        $html = $this->actingAs($u)->get(route('reports.show', $s))->assertOk();
        $html->assertSee('73/100', false)->assertDontSee('27/100', false)
            ->assertSee('+31', false)->assertSee('Pasted code')->assertSee('paste.large')
            ->assertSee('Not observable')->assertSee('Phones and paper notes');
        $this->assertStringContainsString('Odd hours', $html->getContent());
        $this->assertDoesNotMatchRegularExpression('/\+\d*\s*<\/b>\s*Odd hours/', $html->getContent());

        $pdfBody = view('reports._body', ReportModel::build($s))->render();
        $this->assertStringContainsString('73</b>/100', $pdfBody);
        $this->assertStringContainsString('+31 Pasted code', $pdfBody);
        $this->assertStringContainsString('Phones and paper notes', $pdfBody);
        $this->actingAs($u)->get(route('reports.pdf', $s))->assertOk()->assertHeader('content-type', 'application/pdf');

        $csv = $this->actingAs($u)->get(route('reports.csv', $s))->streamedContent();
        $this->assertStringContainsString('confidence,top_reasons,not_observable', $csv);
        $this->assertStringContainsString('PC-01,alice,,,v1,73,high,"+31 Pasted code (paste.large); Odd hours","Phones and paper notes"', $csv);
    }
}
