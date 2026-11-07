<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Appeal;
use App\Services\Audit;
use Illuminate\Http\Request;

/**
 * Instructor Appeals Review & Institutional Overturn-Rate Analytics (P2-C1).
 */
class AppealController extends Controller
{
    public function index(Request $request)
    {
        $statusFilter = $request->string('status')->toString();
        $appeals = Appeal::query()
            ->with(['student', 'session', 'alert', 'score', 'decider:id,name'])
            ->when(in_array($statusFilter, Appeal::STATUSES, true), fn ($q) => $q->where('status', $statusFilter))
            ->orderByRaw("CASE WHEN status = 'open' THEN 0 ELSE 1 END")
            ->latest('id')
            ->get();

        $allAppeals = Appeal::query()->with('alert:id,rule_id,title')->get();
        $counts = [
            'total' => $allAppeals->count(),
            'open' => $allAppeals->where('status', 'open')->count(),
            'upheld' => $allAppeals->where('status', 'upheld')->count(),
            'overturned' => $allAppeals->where('status', 'overturned')->count(),
            'noted' => $allAppeals->where('status', 'noted')->count(),
        ];
        $decidedTotal = $counts['upheld'] + $counts['overturned'] + $counts['noted'];
        $overturnRate = $decidedTotal > 0 ? round($counts['overturned'] / $decidedTotal, 4) : 0.0;

        $overturnedByRule = [];
        foreach ($allAppeals->where('status', 'overturned') as $ap) {
            $ruleId = $ap->alert?->rule_id ?? ($ap->evidence_snapshot['alert']['rule_id'] ?? 'general_session_appeal');
            $overturnedByRule[$ruleId] = ($overturnedByRule[$ruleId] ?? 0) + 1;
        }
        arsort($overturnedByRule);

        $payload = [
            'appeals' => $appeals,
            'status_filter' => $statusFilter,
            'counts' => $counts,
            'decided_total' => $decidedTotal,
            'overturned_count' => $counts['overturned'],
            'overturn_rate' => $overturnRate,
            'overturned_by_rule' => $overturnedByRule,
        ];

        if ($request->expectsJson() || $request->query('format') === 'json') {
            return response()->json($payload);
        }

        return view('appeals.index', $payload);
    }

    public function decide(Request $request, Appeal $appeal)
    {
        $d = $request->validate([
            'status' => 'required|in:upheld,overturned,noted',
            'decision_reason' => 'required|string|min:3|max:2000',
        ]);

        $appeal->update([
            'status' => $d['status'],
            'decision_reason' => $d['decision_reason'],
            'decided_by' => $request->user()->id,
            'decided_at' => now(),
        ]);

        if ($d['status'] === 'overturned' && $appeal->alert_id && $appeal->alert) {
            $appeal->alert->update([
                'acknowledged_by' => $request->user()->id,
                'acknowledged_at' => now(),
                'comment' => '[Overturned] '.$d['decision_reason'],
            ]);
        }

        // Clear automatic legal hold on session/student if no open appeals remain
        $openForSession = Appeal::query()
            ->where('lab_session_id', $appeal->lab_session_id)
            ->where('status', 'open')
            ->exists();
        if (! $openForSession && $appeal->session) {
            $appeal->session->update(['legal_hold' => false]);
        }

        $openForStudent = Appeal::query()
            ->where('student_id', $appeal->student_id)
            ->where('status', 'open')
            ->exists();
        if (! $openForStudent && $appeal->student) {
            $appeal->student->update(['legal_hold' => false]);
        }

        Audit::log('appeal.decided', $appeal, [
            'status' => $d['status'],
            'decision_reason' => $d['decision_reason'],
            'student_id' => $appeal->student_id,
            'lab_session_id' => $appeal->lab_session_id,
            'alert_id' => $appeal->alert_id,
        ]);

        if ($request->expectsJson()) {
            return response()->json([
                'appeal' => $appeal->fresh(['student', 'session', 'alert', 'decider:id,name']),
            ]);
        }

        return back()->with('status', "Appeal #{$appeal->id} marked as {$d['status']}.");
    }
}
