<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Jobs\ScoreSession;
use App\Models\LabSession;
use App\Models\Report;
use App\Models\Student;
use App\Services\Audit;
use App\Services\ReportModel;
use App\Services\ResearchExport;
use Barryvdh\DomPDF\Facade\Pdf;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\StreamedResponse;

/** Integrity reports (per session / per student) with PDF and CSV export, plus research export. */
class ReportController extends Controller
{
    public function index()
    {
        return view('reports.index', [
            'sessions' => LabSession::query()->with('profile')->where('status', 'ended')->latest('ended_at')->paginate(25),
            'reports' => Report::query()->latest()->limit(50)->get()->keyBy('lab_session_id'),
        ]);
    }

    public function show(LabSession $session)
    {
        return view('reports.session', ReportModel::build($session));
    }

    public function showByNumber(int $num)
    {
        $session = LabSession::find($num);

        return $session
            ? view('reports.session', ReportModel::build($session))
            : view('reports.session');
    }

    public function student(LabSession $session, Student $student)
    {
        $data = ReportModel::build($session);
        $data['scores'] = $data['scores']->where('student_id', $student->id)->values();
        $data['student'] = $student;
        $byLogin = Student::forLogins($data['alerts']->pluck('user')->merge($data['evidence']->pluck('user'))->unique());
        $data['alerts'] = $data['alerts']->filter(fn ($a) => ($byLogin[$a->user] ?? null)?->id === $student->id)->values();
        $data['evidence'] = $data['evidence']->filter(fn ($e) => ($byLogin[$e->user] ?? null)?->id === $student->id)->values();

        return view('reports.session', $data);
    }

    public function rescore(LabSession $session)
    {
        ScoreSession::dispatch($session->id);
        Audit::log('report.rescore', $session);

        return back()->with('status', 'Re-scoring queued.');
    }

    public function pdf(LabSession $session)
    {
        Audit::log('report.export_pdf', $session);

        return Pdf::loadView('reports.pdf', ReportModel::build($session))->setPaper('a4')
            ->download("xephyguard-session-{$session->id}.pdf");
    }

    public function csv(LabSession $session): StreamedResponse
    {
        Audit::log('report.export_csv', $session);
        $rows = ReportModel::scoreRows(ReportModel::scores($session));

        return response()->streamDownload(function () use ($rows) {
            $out = fopen('php://output', 'w');
            fputcsv($out, ['pc', 'user', 'student_number', 'student_name', 'method', 'score', 'confidence', 'top_reasons', 'not_observable']);
            foreach ($rows as $r) {
                $reasons = array_map(fn ($x) => trim(($x['points'] !== null ? '+'.$x['points'].' ' : '').$x['label'].($x['signal'] !== '' ? ' ('.$x['signal'].')' : '')), $r['reasons']);
                fputcsv($out, [$r['pc'], $r['user'], $r['student_number'], $r['student_name'], $r['method'], $r['score'],
                    $r['confidence'], implode('; ', $reasons), implode('; ', $r['not_observable'])]);
            }
            fclose($out);
        }, "xephyguard-session-{$session->id}.csv");
    }

    /** Research export: only consenting students, always pseudonymised, no names/logins/student numbers. */
    public function research(Request $request): StreamedResponse
    {
        $d = $request->validate(['from' => 'nullable|date', 'until' => 'nullable|date']);
        Audit::log('report.research_export', null, $d);

        return response()->streamDownload(function () use ($d) {
            $out = fopen('php://output', 'w');
            fputcsv($out, ResearchExport::COLUMNS);
            foreach (ResearchExport::rows($d['from'] ?? null, $d['until'] ?? null) as $row) {
                fputcsv($out, $row);
            }
            fclose($out);
        }, 'xephyguard-research-export.csv');
    }
}
