<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Alert;
use App\Models\EvidenceCapture;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Report;
use App\Models\Score;
use Illuminate\Support\Collection;

/** Data of one session integrity report (F49-F51). Rendered by the HTML, PDF and CSV exports. */
class ReportModel
{
    public static function build(LabSession $session): array
    {
        $scores = self::scores($session);

        return [
            'session' => $session->load('profile'),
            'scores' => $scores,
            'alerts' => Alert::query()->with('pc:id,name')->where('lab_session_id', $session->id)->orderBy('occurred_at')->get(),
            'blocked' => NetEvent::query()->where('lab_session_id', $session->id)->where('action', 'blocked')
                ->selectRaw('pc_id, host, category, count(*) as n, min(occurred_at) as first_at')
                ->groupBy('pc_id', 'host', 'category')->orderByDesc('n')->with('pc:id,name')->get(),
            'pastes' => PasteEvent::query()->where('lab_session_id', $session->id)->where('is_copy', false)
                ->where('chars', '>=', 300)->orderBy('occurred_at')->get(),
            'evidence' => EvidenceCapture::query()->with(['pc:id,name', 'capturer:id,name'])
                ->where('lab_session_id', $session->id)->orderBy('occurred_at')->get(),
            'report' => Report::query()->where('lab_session_id', $session->id)->where('kind', 'session')->first(),
        ];
    }

    /** Scores of one session, highest first. */
    public static function scores(LabSession $session): Collection
    {
        return Score::query()->with(['pc:id,name', 'student'])->where('lab_session_id', $session->id)
            ->orderByDesc('score')->get();
    }

    /**
     * One row per score for the HTML, PDF and CSV renderers. The score and every reason are shown as stored
     * (no inversion, no default points); `points` is null when a reason carries none.
     *
     * @return list<array{pc: string, user: string, student_number: string, student_name: string, method: string, score: int, confidence: string, reasons: list<array{points: ?int, label: string, signal: string}>, not_observable: list<string>}>
     */
    public static function scoreRows(Collection $scores): array
    {
        return $scores->map(fn (Score $s) => [
            'pc' => (string) ($s->pc?->name ?? 'PC-'.$s->pc_id),
            'user' => (string) $s->user,
            'student_number' => (string) ($s->student?->student_number ?? ''),
            'student_name' => (string) ($s->student?->name ?? ''),
            'method' => (string) $s->method,
            'score' => (int) $s->score,
            'confidence' => (string) $s->confidence,
            'reasons' => collect($s->reasons)->filter(fn ($r) => is_array($r))->map(fn (array $r) => [
                'points' => isset($r['points']) && is_numeric($r['points']) ? (int) $r['points'] : null,
                'label' => (string) ($r['label'] ?? ''),
                'signal' => (string) ($r['signal'] ?? ''),
            ])->values()->all(),
            'not_observable' => collect($s->not_observable)->filter(fn ($n) => is_string($n))->values()->all(),
        ])->all();
    }
}
