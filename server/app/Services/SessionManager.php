<?php

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

        return $session;
    }

    public function stop(LabSession $session, ?User $by = null): void
    {
        $this->finish($session, $by, true);
    }

    private function finish(LabSession $session, ?User $by, bool $propagate): void
    {
        if ($session->status === 'ended') {
            return;
        }
        $session->forceFill(['status' => 'ended', 'ended_at' => now()])->save();
        Audit::log('session.stop', $session, ['by' => $by?->email ?? 'timetable']);
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
        $from = $session->profile?->name;
        $session->forceFill(['profile_id' => $profile->id, 'mode' => $profile->mode])->save();
        Audit::log('session.switch_mode', $session, ['from' => $from, 'to' => $profile->name, 'mode' => $profile->mode]);
        $this->propagate($session->fresh());
    }

    /** Per-PC override (e.g. an approved accommodation). Always audited with a reason. */
    public function setOverride(LabSession $session, Pc $pc, ?Profile $profile, string $reason, ?User $by = null): void
    {
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
        $cutoff = now()->subSeconds(config('xephyguard.heartbeat_timeout_s'));
        $n = 0;
        $silent = Pc::query()->active()->whereNotNull('last_seen_at')->where('last_seen_at', '<', $cutoff)
            ->whereNull('silent_alerted_at')->get();
        foreach ($silent as $pc) {
            $pc->silent_alerted_at = now();
            $was = $pc->status;
            $pc->status = 'offline';
            $pc->save();
            broadcast(new PcUpdated($pc));
            if ($pc->clean_shutdown || $was === 'offline') {
                continue;
            }
            $session = LabSession::activeFor($pc->lab_id);
            $alert = Alert::create([
                'pc_id' => $pc->id, 'lab_session_id' => $session?->id, 'rule_id' => 'tamper.machine_silent',
                'severity' => $session ? 'high' : 'low', 'category' => 'tamper',
                'title' => 'Machine silent: no heartbeat since '.$pc->last_seen_at->format('H:i:s').' (agent killed, network cut or power off)',
                'details' => ['last_seen' => $pc->last_seen_at->toIso8601String()], 'user' => $pc->current_user,
                'occurred_at' => now(),
            ]);
            broadcast(new AlertRaised($alert));
            $n++;
        }

        return $n;
    }
}
