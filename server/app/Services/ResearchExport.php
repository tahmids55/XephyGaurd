<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Score;
use Illuminate\Database\Query\Builder;
use Illuminate\Support\LazyCollection;

/**
 * The one research export (F52). Rows of students whose latest research consent is granted,
 * identified only by their pseudonym (ETHICS: research data never holds names, logins or
 * student numbers; the pseudonymize setting does not apply here). Consent is filtered in SQL
 * and rows are streamed in chunks, so memory does not grow with the number of scores.
 */
class ResearchExport
{
    public const COLUMNS = ['session_id', 'course', 'mode', 'condition', 'group', 'participant', 'method', 'score', 'confidence', 'features_json'];

    private const CHUNK = 500;

    /** @return LazyCollection<int, list<mixed>> one CSV row per score, in COLUMNS order */
    public static function rows(?string $from = null, ?string $until = null): LazyCollection
    {
        return Score::query()
            ->join('lab_sessions', 'lab_sessions.id', '=', 'scores.lab_session_id')
            ->join('students', 'students.id', '=', 'scores.student_id')
            ->whereExists(fn (Builder $q) => self::grantedLatestConsent($q))
            ->when($from, fn ($q, $v) => $q->where('lab_sessions.starts_at', '>=', $v))
            ->when($until, fn ($q, $v) => $q->where('lab_sessions.starts_at', '<=', $v))
            ->select('scores.id', 'scores.lab_session_id', 'scores.method', 'scores.score', 'scores.confidence',
                'scores.features', 'lab_sessions.mode', 'lab_sessions.course', 'lab_sessions.experiment_condition',
                'lab_sessions.experiment_group', 'students.pseudonym')
            ->lazyById(self::CHUNK, 'scores.id', 'id')
            ->map(fn (Score $s) => [$s->lab_session_id, $s->course, $s->mode, $s->experiment_condition,
                $s->experiment_group, $s->pseudonym, $s->method, $s->score, $s->confidence, json_encode($s->features)]);
    }

    /**
     * A granted research consent of the score's student with no newer research consent row.
     * Same order as Student::latestConsent (latestOfMany): latest recorded_at, then highest id.
     */
    private static function grantedLatestConsent(Builder $q): void
    {
        $q->selectRaw('1')->from('consents as c')
            ->whereColumn('c.student_id', 'scores.student_id')
            ->where('c.purpose', 'research')
            ->where('c.granted', true)
            ->whereNotExists(fn (Builder $n) => $n->selectRaw('1')->from('consents as n')
                ->whereColumn('n.student_id', 'c.student_id')
                ->where('n.purpose', 'research')
                ->where(fn (Builder $w) => $w->whereColumn('n.recorded_at', '>', 'c.recorded_at')
                    ->orWhere(fn (Builder $t) => $t->whereColumn('n.recorded_at', 'c.recorded_at')->whereColumn('n.id', '>', 'c.id'))));
    }
}
