<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Events\PcUpdated;
use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\AuditLog;
use App\Models\Command;
use App\Models\EvidenceCapture;
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
    public function show(Request $request, Alert $alert)
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
            'has_snapshot' => $alert->snapshot()->exists(),
            'screenshot' => $this->screenshotState($alert, $request->user()?->hasRole('instructor') ?? false),
        ]);
    }

    /**
     * F123: the auto screenshot for this alert. Status only for assistants; the image link (audited
     * `evidence.view`, instructor+ like F46) only for instructors. Null when none was asked for.
     *
     * @return array<string, mixed>|null
     */
    private function screenshotState(Alert $alert, bool $canView): ?array
    {
        $cap = EvidenceCapture::query()->where('alert_id', $alert->id)->where('source_mode', 'auto')->latest('id')->first();
        if ($cap) {
            return ['status' => 'captured', 'can_view' => $canView, 'id' => $cap->id,
                'at' => $cap->occurred_at?->toIso8601String(), 'size_bytes' => $cap->size_bytes, 'sha256' => $cap->sha256,
                'url' => $canView ? route('evidence.show', $cap) : null];
        }
        $cmd = Command::query()->where('pc_id', $alert->pc_id)->where('type', 'evidence_snapshot')
            ->where('args->alert_id', $alert->id)->latest('id')->first(['id', 'created_at']);
        if (! $cmd) {
            return null;
        }
        $masked = AuditLog::query()->where('action', 'pc.evidence.auto_masked')->where('details->command_id', $cmd->id)->exists();

        return ['status' => $masked ? 'masked' : 'requested', 'can_view' => $canView, 'at' => $cmd->created_at?->toIso8601String()];
    }

    /**
     * F123: the context frozen when the alert was raised (copies of rows the CCC already held).
     * Loaded only on request and audited, with its sha256 check result.
     */
    public function snapshot(Alert $alert)
    {
        $snap = $alert->snapshot;
        abort_if($snap === null, 404);
        $d = $snap->data();
        Audit::log('alert.snapshot.view', $alert, ['alert_id' => $alert->id, 'snapshot_id' => $snap->id]);

        $procs = (array) ($d['processes']['list'] ?? []);
        $names = array_values(array_unique(array_filter(array_map(
            fn ($p) => is_array($p) ? (string) ($p['name'] ?? $p['exe'] ?? '') : '', $procs))));
        sort($names, SORT_NATURAL | SORT_FLAG_CASE);

        return response()->json([
            'captured_at' => $snap->captured_at?->toIso8601String(),
            'sha256' => $snap->sha256,
            'intact' => $snap->intact(),
            'pc' => $d['pc'] ?? null,
            'processes' => isset($d['processes']) ? ['at' => $d['processes']['at'] ?? null, 'count' => count($procs), 'names' => $names] : null,
            'events' => array_map(fn ($e) => ['type' => (string) ($e['type'] ?? ''), 'at' => $e['at'] ?? null,
                'summary' => self::summary($e['data'] ?? null)], (array) ($d['events'] ?? [])),
            'net' => array_map(fn ($n) => ['host' => $n['host'] ?? null, 'path' => $n['path'] ?? null, 'layer' => $n['layer'] ?? null,
                'action' => $n['action'] ?? null, 'category' => $n['category'] ?? null, 'at' => $n['at'] ?? null], (array) ($d['net'] ?? [])),
        ]);
    }

    /** One short line of an event's scalar fields ("app=firefox · title=…"), for the snapshot list. */
    private static function summary(mixed $data): string
    {
        if (! is_array($data)) {
            return '';
        }
        $parts = [];
        foreach ($data as $k => $v) {
            if (is_scalar($v) && ! is_bool($v) && (string) $v !== '') {
                $parts[] = $k.'='.$v;
            }
        }

        return mb_strimwidth(implode(' · ', $parts), 0, 160, '…');
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
