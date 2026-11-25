<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Events\PcUpdated;
use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\SessionPc;
use App\Services\Audit;
use App\Services\EventIngestor;
use Illuminate\Http\Request;

class AlertController extends Controller
{
    public function index(Request $request)
    {
        $sessionId = $request->integer('session');
        $base = Alert::query()->when($sessionId, fn ($q) => $q->where('lab_session_id', $sessionId));
        $openOnly = $request->boolean('open');

        $sevQuery = clone $base;
        if ($openOnly) {
            $sevQuery->whereNull('acknowledged_at');
        }
        $sevCounts = $sevQuery->selectRaw('severity, count(*) as n')->groupBy('severity')->pluck('n', 'severity');
        $sevTotals = [];
        foreach (['critical', 'high', 'medium', 'low', 'info'] as $sName) {
            $sevTotals[$sName] = (int) ($sevCounts[$sName] ?? 0);
        }

        $q = (clone $base)->with(['pc:id,name,retired_at', 'acknowledger:id,name'])->latest('occurred_at');
        if ($sev = $request->string('severity')->toString()) {
            $q->where('severity', $sev);
        }
        if ($openOnly) {
            $q->whereNull('acknowledged_at');
        }

        return view('alerts.index', [
            'alerts' => $q->paginate(100)->withQueryString(),
            'openTotal' => (clone $base)->whereNull('acknowledged_at')->count(),
            'sevTotals' => $sevTotals,
            'sessions' => LabSession::query()->latest('starts_at')->limit(30)->get(['id', 'course', 'starts_at', 'mode']),
            'filters' => $request->only(['session', 'severity', 'open']),
        ]);
    }

    /** JSON for the alert details popup: what the agent reported (allow-listed at ingest), session and review history. */
    public function show(Alert $alert)
    {
        $alert->load(['pc:id,name,os,retired_at', 'session:id,course,title,mode', 'acknowledger:id,name', 'reviews.user:id,name']);
        $d = is_array($alert->details) ? $alert->details : [];

        return response()->json([
            'id' => $alert->id,
            'pc' => $alert->pc?->name, 'pc_id' => $alert->pc_id, 'os' => $alert->pc?->os,
            'user' => $alert->user,
            'severity' => $alert->severity, 'rule_id' => $alert->rule_id, 'category' => $alert->category,
            'title' => $alert->title,
            'occurred_at' => $alert->occurred_at?->toIso8601String(),
            'session' => $alert->session ? [
                'id' => $alert->session->id,
                'name' => trim(($alert->session->course ?? '').' '.($alert->session->title ?? '')) ?: 'Session '.$alert->session->id,
                'mode' => $alert->session->mode,
            ] : null,
            'details' => $d,
            'acked' => $alert->acknowledged_at !== null,
            'acked_at' => $alert->acknowledged_at?->toIso8601String(),
            'by' => $alert->acknowledger?->name,
            'note' => $alert->comment,
            'reviews' => $alert->reviews->map(fn ($r) => [
                'by' => $r->user?->name, 'note' => $r->note, 'at' => $r->reviewed_at?->toIso8601String(),
            ])->values(),
        ]);
    }

    public function acknowledge(Request $request, Alert $alert, EventIngestor $ingestor)
    {
        $d = $request->validate(['comment' => 'nullable|string|max:2000']);
        $done = $alert->acknowledgeBy($request->user()->id, $d['comment'] ?? null);
        if ($done) {
            Audit::log('alert.ack', $alert, ['comment' => $d['comment'] ?? null]);
        }
        $tile = $alert->pc ? $this->syncPcLiveState($alert->pc, $ingestor) : null;

        return $request->expectsJson()
            ? response()->json(['ok' => true, 'already' => ! $done, 'pc' => $tile])
            : back()->with('status', $done ? 'Alert acknowledged.' : 'Alert was already acknowledged.');
    }

    public function acknowledgeBatch(Request $request, EventIngestor $ingestor)
    {
        $d = $request->validate([
            'ids' => 'required|array|min:1|max:500',
            'ids.*' => 'integer',
            'comment' => 'nullable|string|max:2000',
        ]);
        $labId = Lab::current()->id;
        $alerts = Alert::query()->whereIn('id', $d['ids'])->whereNull('acknowledged_at')
            ->whereHas('pc', fn ($q) => $q->where('lab_id', $labId))->get();
        $userId = $request->user()->id;
        $alerts = $alerts->filter(function (Alert $alert) use ($userId, $d) {
            if (! $alert->acknowledgeBy($userId, $d['comment'] ?? null)) {
                return false;
            }
            Audit::log('alert.ack', $alert, ['comment' => $d['comment'] ?? null]);

            return true;
        });
        $pcs = Pc::query()->whereIn('id', $alerts->pluck('pc_id')->unique()->filter())->get();
        $tiles = $pcs->map(fn (Pc $pc) => $this->syncPcLiveState($pc, $ingestor))->values()->all();

        return response()->json(['ok' => true, 'pcs' => $tiles]);
    }

    private function syncPcLiveState(Pc $pc, EventIngestor $ingestor): array
    {
        $activeSession = LabSession::activeFor($pc->lab_id);
        $inSession = $activeSession && SessionPc::query()
            ->where('lab_session_id', $activeSession->id)
            ->where('pc_id', $pc->id)
            ->exists();
        $ingestor->refreshLiveScore($pc, $inSession ? $activeSession : null);
        $pc->save();
        broadcast(new PcUpdated($pc));

        return $pc->tile();
    }
}
