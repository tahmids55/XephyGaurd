<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\Appeal;
use App\Models\Event;
use App\Models\LabSession;
use App\Models\Score;
use App\Models\SessionPc;
use App\Models\Student;
use App\Models\StudentPcAssignment;
use App\Services\Audit;
use Illuminate\Http\Request;

/**
 * Student Self-Service Transparency Portal & Appeals Submission (P2-C1, P4-C1).
 * Authenticates students via their unique `portal_token` without requiring staff accounts.
 */
class StudentPortalController extends Controller
{
    public function index(Request $request)
    {
        $token = trim((string) $request->query('token', ''));
        if ($token === '') {
            if ($request->query('format') === 'json') {
                return response()->json(['error' => 'Portal token required'], 401);
            }

            return view('public.student-portal', ['student' => null]);
        }

        $student = Student::query()->where('portal_token', $token)->first();
        if (! $student) {
            if ($request->expectsJson()) {
                return response()->json(['error' => 'Invalid portal token'], 403);
            }

            return view('public.student-portal', ['student' => null, 'invalidToken' => true]);
        }

        $payload = $this->buildStudentData($student);

        if ($request->expectsJson() || $request->query('format') === 'json') {
            return response()->json($payload);
        }

        return view('public.student-portal', $payload + ['token' => $token]);
    }

    public function export(Request $request)
    {
        $token = trim((string) $request->query('token', ''));
        $student = $token !== '' ? Student::query()->where('portal_token', $token)->first() : null;
        abort_unless($student !== null, 403, 'Invalid student portal token.');

        $payload = $this->buildStudentData($student);
        Audit::log('student_portal.export', $student, ['student_number' => $student->student_number]);

        return response()->json($payload, 200, [
            'Content-Disposition' => 'attachment; filename="xephyguard-student-data-'.$student->student_number.'.json"',
        ], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
    }

    public function submitAppeal(Request $request)
    {
        $d = $request->validate([
            'token' => 'required|string|max:64',
            'lab_session_id' => 'required|exists:lab_sessions,id',
            'student_statement' => 'required|string|min:5|max:4000',
            'alert_id' => 'nullable|exists:alerts,id',
            'score_id' => 'nullable|exists:scores,id',
        ]);

        $student = Student::query()->where('portal_token', $d['token'])->first();
        abort_unless($student !== null, 403, 'Invalid student portal token.');

        $session = LabSession::query()->findOrFail((int) $d['lab_session_id']);
        $alert = isset($d['alert_id']) ? Alert::query()->find((int) $d['alert_id']) : null;
        $score = isset($d['score_id']) ? Score::query()->find((int) $d['score_id']) : null;

        $snapshot = [
            'captured_at' => now()->toIso8601String(),
            'session' => [
                'id' => $session->id,
                'course' => $session->course,
                'mode' => $session->mode,
                'ai_policy' => $session->ai_policy ?? 'forbidden',
            ],
            'alert' => $alert ? [
                'id' => $alert->id,
                'rule_id' => $alert->rule_id,
                'severity' => $alert->severity,
                'title' => $alert->title,
                'evidence_meta' => $alert->evidence_meta,
            ] : null,
            'score' => $score ? [
                'id' => $score->id,
                'score' => $score->score,
                'confidence' => $score->confidence,
                'reasons' => $score->reasons,
            ] : null,
        ];

        $appeal = Appeal::create([
            'lab_session_id' => $session->id,
            'student_id' => $student->id,
            'alert_id' => $alert?->id,
            'score_id' => $score?->id,
            'status' => 'open',
            'student_statement' => $d['student_statement'],
            'evidence_snapshot' => $snapshot,
        ]);

        // Automatic legal hold on both student and session until appeal is resolved
        $student->update(['legal_hold' => true]);
        $session->update(['legal_hold' => true]);

        Audit::log('appeal.submitted', $appeal, [
            'student_id' => $student->id,
            'student_number' => $student->student_number,
            'lab_session_id' => $session->id,
            'alert_id' => $alert?->id,
            'score_id' => $score?->id,
            'legal_hold' => true,
        ]);

        if ($request->expectsJson()) {
            return response()->json([
                'appeal' => $appeal,
                'legal_hold' => true,
            ], 201);
        }

        return redirect('/student-portal?token='.urlencode($student->portal_token))
            ->with('status', 'Appeal submitted. A legal hold has been placed on your session records until your instructor completes the review.');
    }

    /**
     * Collects all sessions, scores, alerts, events, work journal entries, and appeals for a student.
     */
    public function buildStudentData(Student $student): array
    {
        $login = strtolower($student->login);
        $assignedSessionIds = StudentPcAssignment::query()
            ->where('student_id', $student->id)
            ->whereNotNull('lab_session_id')
            ->pluck('lab_session_id');
        $sessionPcIds = SessionPc::query()
            ->where('student_id', $student->id)
            ->pluck('lab_session_id');
        $scoreSessionIds = Score::query()
            ->where(fn ($q) => $q->where('student_id', $student->id)->orWhere('user', $login))
            ->pluck('lab_session_id');
        $alertSessionIds = Alert::query()
            ->where('user', $login)
            ->whereNotNull('lab_session_id')
            ->pluck('lab_session_id');
        $eventSessionIds = Event::query()
            ->where('user', $login)
            ->whereNotNull('lab_session_id')
            ->distinct()
            ->pluck('lab_session_id');
        $appealSessionIds = Appeal::query()
            ->where('student_id', $student->id)
            ->pluck('lab_session_id');

        $sessionIds = $assignedSessionIds
            ->merge($sessionPcIds)
            ->merge($scoreSessionIds)
            ->merge($alertSessionIds)
            ->merge($eventSessionIds)
            ->merge($appealSessionIds)
            ->filter()
            ->unique()
            ->values();

        $sessions = LabSession::query()
            ->with('profile')
            ->whereIn('id', $sessionIds)
            ->orderByDesc('starts_at')
            ->get();

        $scores = Score::query()
            ->with('pc:id,name')
            ->where(fn ($q) => $q->where('student_id', $student->id)->orWhere('user', $login))
            ->orderByDesc('id')
            ->get();

        $alerts = Alert::query()
            ->with('pc:id,name')
            ->where('user', $login)
            ->orderByDesc('occurred_at')
            ->get();

        $events = Event::query()
            ->with('pc:id,name')
            ->where('user', $login)
            ->orderByDesc('occurred_at')
            ->limit(250)
            ->get();

        $workJournal = $events
            ->filter(fn (Event $e) => $e->type === 'file_activity' || $e->event_class === 'disclosure' || ($e->data['ai_policy'] ?? '') === 'permitted_with_disclosure')
            ->sortBy('occurred_at')
            ->values()
            ->map(fn (Event $e) => [
                'id' => $e->id,
                'lab_session_id' => $e->lab_session_id,
                'pc' => $e->pc?->name,
                'type' => $e->type,
                'event_class' => $e->event_class,
                'confidence' => $e->confidence,
                'occurred_at' => $e->occurred_at?->toIso8601String(),
                'summary' => $e->type === 'file_activity'
                    ? sprintf('Work folder save: %s (%d files, %d bytes)', $e->data['dir'] ?? 'work_dir', (int) ($e->data['files_changed'] ?? $e->data['count'] ?? 1), (int) ($e->data['total_bytes'] ?? $e->data['bytes'] ?? 0))
                    : sprintf('Disclosed AI tool: %s (%s)', $e->data['description'] ?? $e->data['tool'] ?? $e->data['rule_id'] ?? $e->type, $e->data['subject'] ?? $e->data['ide'] ?? 'permitted'),
                'data' => $e->data,
                'evidence_meta' => $e->evidence_meta,
            ])
            ->all();

        $appeals = Appeal::query()
            ->with(['session', 'alert', 'score', 'decider:id,name'])
            ->where('student_id', $student->id)
            ->orderByDesc('id')
            ->get();

        return [
            'student' => [
                'id' => $student->id,
                'student_number' => $student->student_number,
                'name' => $student->name,
                'login' => $student->login,
                'pseudonym' => $student->pseudonym,
                'exempt' => $student->exempt,
                'exempt_reason' => $student->exempt_reason,
                'legal_hold' => $student->legal_hold,
            ],
            'studentModel' => $student,
            'sessions' => $sessions,
            'scores' => $scores,
            'alerts' => $alerts,
            'events' => $events,
            'work_journal' => $workJournal,
            'appeals' => $appeals,
            'generated_at' => now()->toIso8601String(),
        ];
    }
}
