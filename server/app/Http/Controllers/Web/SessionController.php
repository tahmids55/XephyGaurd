<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Command;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\Setting;
use App\Services\Audit;
use App\Services\CommandBus;
use App\Services\SessionManager;
use App\Services\SessionNotActive;
use Illuminate\Http\Request;

/** Session control: start/stop, switch mode, broadcast message, isolate, kill, per-PC override. */
class SessionController extends Controller
{
    public function __construct(private readonly SessionManager $sessions, private readonly CommandBus $commands) {}

    public function index()
    {
        $lab = Lab::current();

        return view('sessions.index', [
            'active' => LabSession::activeFor($lab->id)?->load(['profile', 'sessionPcs.pc', 'sessionPcs.overrideProfile']),
            'history' => LabSession::query()->with('profile')->where('lab_id', $lab->id)->latest('starts_at')->paginate(20),
            'profiles' => Profile::query()->orderBy('mode')->get(),
            'commands' => Command::query()->with('pc:id,name')->latest()->limit(20)->get(),
            // The network lock is enforced by the agent only in proxy-only mode (DECISIONS #23).
            'proxyOnly' => Setting::get('network_mode', config('xephyguard.network_mode', 'gateway')) === 'proxy_only',
        ]);
    }

    public function start(Request $request)
    {
        $d = $request->validate([
            'course' => 'required|string|max:64', 'profile_id' => 'required|exists:profiles,id',
            'duration_min' => 'nullable|integer|min:5|max:720',
            'experiment_condition' => 'nullable|in:baseline,treatment', 'experiment_group' => 'nullable|string|max:32',
            'network_lock' => 'nullable|in:'.implode(',', LabSession::NETWORK_LOCKS),
        ]);
        $this->sessions->start(Lab::current(), Profile::findOrFail($d['profile_id']), $d['course'], $request->user(),
            isset($d['duration_min']) ? now()->addMinutes((int) $d['duration_min']) : null,
            array_filter(['experiment_condition' => $d['experiment_condition'] ?? null, 'experiment_group' => $d['experiment_group'] ?? null,
                'network_lock' => $d['network_lock'] ?? null]));

        return back()->with('status', 'Session started. PCs switch within seconds.');
    }

    public function stop(Request $request, LabSession $session)
    {
        $this->sessions->stop($session, $request->user());

        return back()->with('status', 'Session ended. Blocking relaxed; report is being generated.');
    }

    public function networkLock(Request $request, LabSession $session)
    {
        $d = $request->validate(['network_lock' => 'required|in:'.implode(',', LabSession::NETWORK_LOCKS)]);
        try {
            $this->sessions->setNetworkLock($session, $d['network_lock'], $request->user());
        } catch (SessionNotActive $e) {
            abort(422, $e->getMessage());
        }

        return back()->with('status', 'Network lock set to '.$d['network_lock'].'. PCs switch within seconds.');
    }

    public function switchProfile(Request $request, LabSession $session)
    {
        $d = $request->validate(['profile_id' => 'required|exists:profiles,id']);
        try {
            $this->sessions->switchProfile($session, Profile::findOrFail($d['profile_id']), $request->user());
        } catch (SessionNotActive $e) {
            abort(422, $e->getMessage());
        }

        return back()->with('status', 'Mode switched.');
    }

    public function message(Request $request)
    {
        $d = $request->validate(['text' => 'required|string|max:500', 'pc_ids' => 'nullable|array', 'pc_ids.*' => 'integer',
            'urgent' => 'nullable|boolean']);
        $pcs = Pc::query()->active()->where('lab_id', Lab::current()->id)
            ->when(! empty($d['pc_ids']), fn ($q) => $q->whereIn('id', $d['pc_ids']))->get();
        $n = $this->commands->sendMany($pcs, 'message', ['text' => $d['text'], 'title' => 'Message from your instructor',
            'urgent' => (bool) ($d['urgent'] ?? false)], $request->user());
        Audit::log('session.message', null, ['text' => $d['text'], 'pcs' => $n]);

        return $request->expectsJson() ? response()->json(['sent' => $n]) : back()->with('status', "Message sent to $n PCs.");
    }

    public function isolate(Request $request, Pc $pc)
    {
        $d = $request->validate(['isolated' => 'required|boolean', 'reason' => 'nullable|string|max:200']);
        $this->sessions->isolate($pc, (bool) $d['isolated'], $request->user(), $d['reason'] ?? '');

        return $request->expectsJson() ? response()->json(['ok' => true]) : back();
    }

    public function kill(Request $request, Pc $pc)
    {
        $d = $request->validate(['exe' => 'required_without:pid|nullable|string|max:128', 'pid' => 'nullable|integer|min:1',
            'start_time' => 'nullable|integer|min:1']);
        // start_time (from process_snapshot.list[].start) pins the exact process: a reused pid is never killed.
        $args = array_filter(['exe' => $d['exe'] ?? null, 'pid' => $d['pid'] ?? null,
            'start_time' => isset($d['pid']) ? ($d['start_time'] ?? null) : null]);
        $cmd = $this->commands->send($pc, 'kill', $args, $request->user());
        Audit::log('pc.kill', $pc, $cmd->args);

        return $request->expectsJson() ? response()->json(['command_id' => $cmd->id]) : back()->with('status', 'Kill command sent.');
    }

    public function rescan(Request $request, Pc $pc)
    {
        $d = $request->validate([
            'what' => 'nullable|in:processes,network,ports,devices,extensions,artifacts,all',
            'kind' => 'nullable|in:processes,network,ports,devices,extensions,artifacts,all',
        ]);
        $what = $d['what'] ?? $d['kind'] ?? 'processes';
        $cmd = $this->commands->send($pc, 'rescan', ['what' => $what], $request->user());

        return $request->expectsJson() ? response()->json(['command_id' => $cmd->id]) : back()->with('status', 'Rescan requested.');
    }

    public function override(Request $request, LabSession $session, Pc $pc)
    {
        $d = $request->validate(['profile_id' => 'nullable|exists:profiles,id', 'reason' => 'required|string|max:200']);
        try {
            $this->sessions->setOverride($session, $pc, isset($d['profile_id']) ? Profile::find($d['profile_id']) : null,
                $d['reason'], $request->user());
        } catch (SessionNotActive $e) {
            abort(422, $e->getMessage());
        }

        return back()->with('status', 'Override saved (audited).');
    }
}
