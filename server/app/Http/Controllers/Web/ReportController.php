<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Jobs\ScoreSession;
use App\Models\Alert;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Report;
use App\Models\Score;
use App\Models\Setting;
use App\Models\Student;
use App\Services\Audit;
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

    private function data(LabSession $session): array
    {
        $scores = Score::query()->with(['pc:id,name', 'student'])->where('lab_session_id', $session->id)
            ->orderByDesc('score')->get();

        return [
            'session' => $session->load('profile'),
            'scores' => $scores,
            'alerts' => Alert::query()->with('pc:id,name')->where('lab_session_id', $session->id)->orderBy('occurred_at')->get(),
            'blocked' => NetEvent::query()->where('lab_session_id', $session->id)->where('action', 'blocked')
                ->selectRaw('pc_id, host, category, count(*) as n, min(occurred_at) as first_at')
                ->groupBy('pc_id', 'host', 'category')->orderByDesc('n')->with('pc:id,name')->get(),
            'pastes' => PasteEvent::query()->where('lab_session_id', $session->id)->where('is_copy', false)
                ->where('chars', '>=', 300)->orderBy('occurred_at')->get(),
            'report' => Report::query()->where('lab_session_id', $session->id)->where('kind', 'session')->first(),
        ];
    }

    public function show(LabSession $session)
    {
        return view('reports.session', $this->data($session));
    }

    public function student(LabSession $session, Student $student)
    {
        $data = $this->data($session);
        $data['scores'] = $data['scores']->where('student_id', $student->id)->values();
        $data['student'] = $student;
        $data['alerts'] = $data['alerts']->filter(fn ($a) => Student::forLogin($a->user)?->id === $student->id)->values();

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

        return Pdf::loadView('reports.pdf', $this->data($session))->setPaper('a4')
            ->download("xephyguard-session-{$session->id}.pdf");
    }

    public function csv(LabSession $session): StreamedResponse
    {
        Audit::log('report.export_csv', $session);
        $scores = Score::query()->with(['pc:id,name', 'student'])->where('lab_session_id', $session->id)->get();

        return response()->streamDownload(function () use ($scores) {
            $out = fopen('php://output', 'w');
            fputcsv($out, ['pc', 'user', 'student_number', 'student_name', 'method', 'score', 'confidence', 'top_reasons']);
            foreach ($scores as $s) {
                fputcsv($out, [$s->pc?->name, $s->user, $s->student?->student_number, $s->student?->name, $s->method, $s->score,
                    $s->confidence, collect($s->reasons)->take(3)->pluck('label')->implode('; ')]);
            }
            fclose($out);
        }, "xephyguard-session-{$session->id}.csv");
    }

    /** Research export: only consenting students, pseudonymized, no names/logins. */
    public function research(Request $request): StreamedResponse
    {
        $d = $request->validate(['from' => 'nullable|date', 'until' => 'nullable|date']);
        Audit::log('report.research_export', null, $d);
        $pseudo = (bool) Setting::get('pseudonymize_exports', true);
        $rows = Score::query()->with(['student.latestConsent', 'pc:id,name'])
            ->join('lab_sessions', 'lab_sessions.id', '=', 'scores.lab_session_id')
            ->when($d['from'] ?? null, fn ($q, $v) => $q->where('lab_sessions.starts_at', '>=', $v))
            ->when($d['until'] ?? null, fn ($q, $v) => $q->where('lab_sessions.starts_at', '<=', $v))
            ->select('scores.*', 'lab_sessions.mode', 'lab_sessions.course', 'lab_sessions.experiment_condition',
                'lab_sessions.experiment_group', 'lab_sessions.starts_at')->get()
            ->filter(fn ($s) => $s->student && $s->student->consentsToResearch());

        return response()->streamDownload(function () use ($rows, $pseudo) {
            $out = fopen('php://output', 'w');
            fputcsv($out, ['session_id', 'course', 'mode', 'condition', 'group', 'participant', 'method', 'score', 'confidence', 'features_json']);
            foreach ($rows as $s) {
                fputcsv($out, [$s->lab_session_id, $s->course, $s->mode, $s->experiment_condition, $s->experiment_group,
                    $pseudo ? $s->student->pseudonym : $s->student->student_number, $s->method, $s->score, $s->confidence,
                    json_encode($s->features)]);
            }
            fclose($out);
        }, 'xephyguard-research-export.csv');
    }
}
