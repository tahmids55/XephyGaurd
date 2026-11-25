<?php

declare(strict_types=1);

namespace App\Services;

use App\Events\AlertRaised;
use App\Events\PcUpdated;
use App\Events\SessionChanged;
use App\Jobs\ScoreSession;
use App\Models\Alert;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\SessionPc;
use App\Models\TimetableEntry;
use App\Models\User;
use Carbon\CarbonInterface;
use Illuminate\Support\Facades\DB;

/** Starts/stops sessions, switches modes, applies overrides and isolation, runs the timetable. */
class SessionManager
{
    public function __construct(private readonly CommandBus $commands) {}

    private function network(): NetworkPolicyWriter
    {
        return app(NetworkPolicyWriter::class);
    }

    public function start(Lab $lab, Profile $profile, string $course, ?User $by = null, ?CarbonInterface $endsAt = null,
        array $extra = []): LabSession
    {
        $session = DB::transaction(function () use ($lab, $profile, $course, $by, $endsAt, $extra) {
            // One active session per lab: starting a new one ends the previous.
            foreach (LabSession::query()->where('lab_id', $lab->id)->active()->get() as $old) {
                $this->finish($old, $by, false);
            }
            $s = LabSession::create(array_merge([
                'lab_id' => $lab->id, 'profile_id' => $profile->id, 'course' => $course, 'mode' => $profile->mode,
                'status' => 'active', 'starts_at' => now(), 'ends_at' => $endsAt, 'started_at' => now(),
                'started_by' => $by?->id,
            ], $extra));
            foreach (Pc::query()->active()->where('lab_id', $lab->id)->pluck('id') as $pcId) {
                SessionPc::create(['lab_session_id' => $s->id, 'pc_id' => $pcId]);
            }

            return $s;
        });
        Audit::log('session.start', $session, ['course' => $course, 'profile' => $profile->name, 'mode' => $profile->mode]);
        $this->propagate($session);
        $this->reconcileScores($lab->id, $session);

        return $session;
    }

    public function stop(LabSession $session, ?User $by = null): void
    {
        $this->finish($session, $by, true);
        $this->reconcileScores($session->lab_id, null);
    }

    /** The live score counts only the session's alerts while one runs, so it changes when a session starts or ends. */
    private function reconcileScores(int $labId, ?LabSession $session): void
    {
        $pcs = Pc::query()->active()->where('lab_id', $labId)->get();
        app(EventIngestor::class)->reconcileLiveScores($pcs, $session);
    }

    private function finish(LabSession $session, ?User $by, bool $propagate): void
    {
        if ($session->status === 'ended') {
            return;
        }
        $session->forceFill(['status' => 'ended', 'ended_at' => now()])->save();
        Audit::log('session.stop', $session, ['by' => $by?->email ?? 'timetable']);
        app(ScreenGatewayRevoker::class)->session($session->id);
        try {
            ScoreSession::dispatch($session->id);   // scoring + report generation start automatically
        } catch (\Throwable $e) {
            // Only reachable with the synchronous queue (development): never block ending a session.
            report($e);
        }
        if ($propagate) {
            $this->propagate($session);
        }
    }

    public function switchProfile(LabSession $session, Profile $profile, ?User $by = null): void
    {
        $this->requireActive($session);
        $from = $session->profile?->name;
        $session->forceFill(['profile_id' => $profile->id, 'mode' => $profile->mode])->save();
        Audit::log('session.switch_mode', $session, ['from' => $from, 'to' => $profile->name, 'mode' => $profile->mode]);
        $this->propagate($session->fresh());
    }

    /** Changes the proxy-only network lock of a running session (off / observe / strict). Audited. */
    public function setNetworkLock(LabSession $session, string $lock, ?User $by = null): void
    {
        if (! in_array($lock, LabSession::NETWORK_LOCKS, true)) {
            throw new \InvalidArgumentException("unknown network lock $lock");
        }
        $this->requireActive($session);
        $from = $session->network_lock;
        $session->forceFill(['network_lock' => $lock])->save();
        Audit::log('session.network_lock', $session, ['from' => $from, 'to' => $lock, 'by' => $by?->email]);
        $this->propagate($session->fresh());
    }

    /** Per-PC override (e.g. an approved accommodation). Always audited with a reason. */
    public function setOverride(LabSession $session, Pc $pc, ?Profile $profile, string $reason, ?User $by = null): void
    {
        $this->requireActive($session);
        if ((int) $pc->lab_id !== (int) $session->lab_id) {
            throw new SessionNotActive('That PC is not in this session\'s lab.');
        }
        SessionPc::query()->updateOrCreate(['lab_session_id' => $session->id, 'pc_id' => $pc->id],
            ['override_profile_id' => $profile?->id, 'override_reason' => $profile ? $reason : null]);
        Audit::log('session.override', $pc, ['session_id' => $session->id, 'profile' => $profile?->name, 'reason' => $reason]);
        $this->network()->write();
        $this->commands->send($pc, 'policy_updated', [], $by);
    }

    public function isolate(Pc $pc, bool $isolated, ?User $by = null, string $reason = ''): void
    {
        $pc->forceFill(['isolated' => $isolated])->save();
        Audit::log($isolated ? 'pc.isolate' : 'pc.unisolate', $pc, ['reason' => $reason]);
        $this->network()->write();
        $this->commands->send($pc, 'isolate_notice', ['isolated' => $isolated], $by);
        broadcast(new PcUpdated($pc));
    }

    /** Mode switches, locks and overrides only change a running session; ended/scheduled ones are history (S-22). */
    private function requireActive(LabSession $session): void
    {
        if ($session->fresh()?->status !== 'active') {
            throw new SessionNotActive('This session is not running.');
        }
    }

    /** Pushes a session change everywhere: network layers, agents, dashboards. */
    private function propagate(LabSession $session): void
    {
        $this->network()->write();
        $pcs = Pc::query()->active()->where('lab_id', $session->lab_id)->get();
        $this->commands->sendMany($pcs, 'policy_updated');
        broadcast(new SessionChanged($session));
    }

    /** Runs every minute from the scheduler. Returns [started, stopped]. */
    public function timetableTick(?CarbonInterface $now = null): array
    {
        $now ??= now();
        $started = $stopped = 0;
        foreach (LabSession::query()->active()->whereNotNull('ends_at')->where('ends_at', '<=', $now)->get() as $s) {
            $this->stop($s);
            $stopped++;
        }
        foreach (Lab::all() as $lab) {
            if (LabSession::activeFor($lab->id)) {
                continue;
            }
            $entry = TimetableEntry::query()->where('lab_id', $lab->id)->where('enabled', true)->get()
                ->first(fn (TimetableEntry $e) => $e->covers($now));
            if (! $entry) {
                continue;
            }
            $endsAt = $now->copy()->setTimeFromTimeString($entry->end_time);
            // Do not restart an entry that was stopped manually earlier in the same slot.
            $already = LabSession::query()->where('timetable_entry_id', $entry->id)
                ->where('starts_at', '>=', $now->copy()->setTimeFromTimeString($entry->start_time))->exists();
            if ($already) {
                continue;
            }
            $this->start($lab, $entry->profile, $entry->course, null, $endsAt, ['timetable_entry_id' => $entry->id]);
            $started++;
        }

        return [$started, $stopped];
    }

    /** "Machine silent": 3 missed heartbeats without a clean-shutdown announcement. */
    public function detectSilent(): int
    {
        $defaultTimeout = max(10, (int) config('xephyguard.heartbeat_timeout_s', 45));
        $sessions = [];
        $cutoffs = [];
        $timeouts = [];
        foreach (Lab::all() as $lab) {
            $session = LabSession::activeFor($lab->id);
            $sessions[$lab->id] = $session;
            $customHb = (int) data_get($session?->profile?->agent, 'intervals.heartbeat_s', 0);
            $timeout = $customHb > 0 ? max(10, $customHb * 3) : $defaultTimeout;
            $cutoffs[$lab->id] = now()->subSeconds($timeout);
            $timeouts[$lab->id] = $timeout;
        }
        $n = 0;
        $silent = Pc::query()->active()->whereNotNull('last_seen_at')
            ->whereNull('silent_alerted_at')->get()
            ->filter(function (Pc $pc) use ($cutoffs, $defaultTimeout) {
                $cutoff = $cutoffs[$pc->lab_id] ?? now()->subSeconds($defaultTimeout);

                return $pc->last_seen_at->lt($cutoff);
            });
        // Lab snapshot for the health classifier: one query, only when something is silent.
        $neighbours = $silent->isEmpty() ? collect() : Pc::query()->active()->whereNotNull('last_seen_at')
            ->get(['id', 'lab_id', 'last_seen_at', 'clean_shutdown'])->groupBy('lab_id');
        foreach ($silent as $pc) {
            $health = AgentHealthClassifier::assess(
                ['id' => $pc->id, 'last_seen_at' => $pc->last_seen_at, 'sleep_or_shutdown' => $pc->clean_shutdown],
                ($neighbours[$pc->lab_id] ?? collect())->map(fn (Pc $o) => ['id' => $o->id, 'last_seen_at' => $o->last_seen_at])->all(),
                now(),
                $timeouts[$pc->lab_id] ?? $defaultTimeout,
            );
            $pc->silent_alerted_at = now();
            $was = $pc->status;
            $pc->status = 'offline';
            $pc->save();
            broadcast(new PcUpdated($pc));
            if ($pc->clean_shutdown || $was === 'offline') {
                continue;
            }
            $session = $sessions[$pc->lab_id] ?? LabSession::activeFor($pc->lab_id);
            $alert = Alert::create([
                'pc_id' => $pc->id, 'lab_session_id' => $session?->id, 'rule_id' => 'tamper.machine_silent',
                'severity' => $session ? 'high' : 'low', 'category' => 'tamper',
                'title' => 'Machine silent: no heartbeat since '.$pc->last_seen_at->format('H:i:s').' (agent killed, network cut or power off)',
                'details' => [
                    'last_seen' => $pc->last_seen_at->toIso8601String(),
                    'health' => $health['status'],
                    'reasons' => $health['reasons'],
                ], 'user' => $pc->current_user,
                'occurred_at' => now(),
            ]);
            broadcast(new AlertRaised($alert));
            $inSession = $session && SessionPc::query()->where('lab_session_id', $session->id)->where('pc_id', $pc->id)->exists();
            app(EventIngestor::class)->refreshLiveScore($pc, $inSession ? $session : null);
            $pc->save();
            $n++;
        }

        return $n;
    }
}
