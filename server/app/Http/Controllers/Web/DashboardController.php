<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Alert;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\Seat;
use App\Services\Audit;
use App\Services\EventIngestor;
use App\Services\InspectorQueries;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;

class DashboardController extends Controller
{
    public function index(EventIngestor $ingestor)
    {
        $lab = Lab::current();
        $session = LabSession::activeFor($lab->id);
        $pcs = Pc::query()->active()->where('lab_id', $lab->id)->with('agent:id,pc_id,version')->orderBy('name')->get();
        $ingestor->reconcileLiveScores($pcs, $session);

        return view('dashboard.live', [
            'lab' => $lab,
            'session' => $session?->load('profile'),
            'seats' => Seat::query()->where('lab_id', $lab->id)->get(['id', 'label', 'x', 'y']),
            'pcs' => $pcs->map(fn ($p) => $p->tile() + [
                'hostname' => $p->hostname,
                'ip' => $p->ip,
                'os_version' => $p->os_version,
                'capabilities' => $p->capabilities,
                'agent_version' => $p->agent?->version,
            ]),
            'profiles' => Profile::query()->orderBy('mode')->get(['id', 'name', 'mode']),
            'recentAlerts' => Alert::query()->whereNull('acknowledged_at')
                ->with(['pc:id,name,retired_at', 'acknowledger:id,name'])
                ->latest('occurred_at')->limit(500)->get()
                ->concat(
                    Alert::query()->whereNotNull('acknowledged_at')
                        ->with(['pc:id,name,retired_at', 'acknowledger:id,name'])
                        ->latest('acknowledged_at')->limit(30)->get()
                )
                ->map(fn ($a) => $a->broadcastPayload() + [
                    'pc_retired' => $a->pc?->retired_at !== null,
                    'occurred_at_iso' => $a->occurred_at?->toIso8601String(),
                    'ago_min' => $a->occurred_at ? max(0, (int) $a->occurred_at->diffInMinutes(now())) : 0,
                    'acked' => $a->acknowledged_at !== null,
                    'by' => $a->acknowledger?->name ?? '',
                    'note' => $a->comment ?? '',
                ]),
        ]);
    }

    /** Detail drawer: timeline, processes, network log, key-rate chart, paste log. */
    /** Read-only: the live score is kept current where alerts and sessions change (EventIngestor, SessionManager). */
    public function pc(Request $request, Pc $pc, InspectorQueries $inspector): JsonResponse
    {
        $session = LabSession::activeFor($pc->lab_id);

        return response()->json($inspector->forPc($pc, $session, (bool) $request->user()?->hasRole('instructor')));
    }

    /** Seat-map editor: saves the grid layout (drag-and-drop) and PC-to-seat assignment. */
    public function saveLayout(Request $request)
    {
        $lab = Lab::current();
        $d = $request->validate([
            'cols' => 'required|integer|min:1|max:30', 'rows' => 'required|integer|min:1|max:30',
            'tiles' => 'present|array', 'tiles.*.pc_id' => 'required|integer', 'tiles.*.x' => 'required|integer|min:0',
            'tiles.*.y' => 'required|integer|min:0', 'tiles.*.label' => 'nullable|string|max:32',
        ]);
        DB::transaction(function () use ($lab, $d) {
            $lab->update(['grid_cols' => $d['cols'], 'grid_rows' => $d['rows']]);
            Pc::query()->where('lab_id', $lab->id)->update(['seat_id' => null]);
            Seat::query()->where('lab_id', $lab->id)->delete();
            foreach ($d['tiles'] as $t) {
                $seat = Seat::create(['lab_id' => $lab->id, 'x' => $t['x'], 'y' => $t['y'],
                    'label' => $t['label'] ?? chr(65 + $t['y']).($t['x'] + 1)]);
                Pc::query()->where('lab_id', $lab->id)->whereKey($t['pc_id'])->update(['seat_id' => $seat->id]);
            }
        });
        Audit::log('lab.layout', $lab, ['tiles' => count($d['tiles'])]);

        return response()->json(['ok' => true]);
    }
}
