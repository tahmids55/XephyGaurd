<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Services\Audit;
use App\Services\EventIngestor;
use Illuminate\Http\Request;

class AlertController extends Controller
{
    public function index(Request $request)
    {
        $q = Alert::query()->with(['pc:id,name', 'acknowledger:id,name'])->latest('occurred_at');
        if ($s = $request->integer('session')) {
            $q->where('lab_session_id', $s);
        }
        if ($sev = $request->string('severity')->toString()) {
            $q->where('severity', $sev);
        }
        if ($request->boolean('open')) {
            $q->whereNull('acknowledged_at');
        }
        if ($class = $request->string('event_class')->toString()) {
            $q->where('details->event_class', $class);
        }

        $allMatching = (clone $q)->get(['id', 'rule_id', 'category', 'details']);
        $installedCount = $allMatching->filter(fn (Alert $a) => $a->event_class === 'installed')->count();
        $activeCount = $allMatching->filter(fn (Alert $a) => $a->event_class === 'active')->count();
        $blockedAlertCount = $allMatching->filter(fn (Alert $a) => $a->event_class === 'blocked')->count();
        $blockedNetCount = NetEvent::query()
            ->when($request->integer('session'), fn ($nq, $sid) => $nq->where('lab_session_id', $sid))
            ->where('action', 'blocked')
            ->count();

        return view('alerts.index', [
            'alerts' => $q->paginate(50)->withQueryString(),
            'sessions' => LabSession::query()->latest('starts_at')->limit(30)->get(['id', 'course', 'starts_at', 'mode']),
            'filters' => $request->only(['session', 'severity', 'open', 'event_class']),
            'installed_count' => $installedCount,
            'active_count' => $activeCount,
            'blocked_count' => $blockedAlertCount + $blockedNetCount,
        ]);
    }

    public function acknowledge(Request $request, Alert $alert, EventIngestor $ingestor)
    {
        $d = $request->validate(['comment' => 'nullable|string|max:2000']);
        $alert->forceFill(['acknowledged_by' => $request->user()->id, 'acknowledged_at' => now(),
            'comment' => $d['comment'] ?? $alert->comment])->save();
        Audit::log('alert.ack', $alert, ['comment' => $d['comment'] ?? null]);
        $pc = $alert->pc;
        $ingestor->refreshLiveScore($pc, $alert->session);
        $pc->save();
        broadcast(new \App\Events\PcUpdated($pc));

        return $request->expectsJson() ? response()->json(['ok' => true]) : back()->with('status', 'Alert acknowledged.');
    }
}
