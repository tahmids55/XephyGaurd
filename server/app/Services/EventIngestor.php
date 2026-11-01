<?php

namespace App\Services;

use App\Events\AlertRaised;
use App\Events\PcUpdated;
use App\Models\Agent;
use App\Models\Alert;
use App\Models\Command;
use App\Models\Event;
use App\Models\KeyStat;
use App\Models\LabSession;
use App\Models\PasteEvent;
use App\Models\Pc;
use App\Models\SessionPc;
use App\Models\Setting;
use App\Models\SignatureVersion;
use App\Models\Student;
use App\Models\StudentPcAssignment;
use Carbon\CarbonImmutable;
use Illuminate\Support\Facades\DB;
use Illuminate\Validation\ValidationException;

/**
 * Ingests an agent batch (POST /api/agent/v1/events): validates it, de-duplicates by sequence,
 * stores raw events plus derived rows (alerts, key stats, pastes), updates the live tile and
 * returns the acknowledgement the agent uses to trim its offline queue.
 */
class EventIngestor
{
    public const TYPES = ['heartbeat', 'agent_started', 'agent_stopping', 'process_start', 'process_stop',
        'process_snapshot', 'rule_hit', 'listening_port', 'window_focus', 'paste', 'copy', 'key_stats', 'net_adapter',
        'device', 'extension_scan', 'artifact_scan', 'file_activity', 'gpu_sample', 'tamper', 'capabilities',
        'command_result', 'helper_status'];

    private const TAMPER_TITLES = [
        'binary_hash_mismatch' => 'Agent binary does not match the installed package',
        'binary_changed' => 'Agent binary was modified',
        'helper_binary_missing' => 'Session helper binary is missing',
        'config_changed' => 'Agent configuration was edited',
        'ca_removed' => 'Lab CA certificate was removed',
        'ca_changed' => 'Lab CA certificate was replaced',
        'clock_change' => 'System clock was changed',
        'helper_missing' => 'Session helper is not running for the logged-in user',
        'signature_invalid' => 'Agent received an invalid signature bundle',
    ];

    public function __construct(private readonly SessionManager $sessions, private readonly PolicyBuilder $policies) {}

    public function ingest(Agent $agent, array $batch, string $ip): array
    {
        $this->validate($agent, $batch);
        $pc = $agent->pc;
        $session = LabSession::activeFor($pc->lab_id);
        $inSession = $session && SessionPc::query()->where('lab_session_id', $session->id)->where('pc_id', $pc->id)->exists();
        $session = $inSession ? $session : null;
        $alerts = [];
        $tileBefore = json_encode($pc->tile());

        DB::transaction(function () use ($agent, $pc, $batch, $ip, $session, &$alerts) {
            $hb = $batch['heartbeat'] ?? [];
            $offset = 0;
            if (isset($hb['local_time_ms']) && is_int($hb['local_time_ms'])) {
                $offset = (int) (now()->getTimestampMs() - $hb['local_time_ms']);
                $agent->clock_offset_ms = $offset;
            }
            $this->applyHeartbeat($agent, $pc, $hb, $ip, $session);

            $events = collect($batch['events'] ?? [])->filter(fn ($e) => $e['seq'] > $agent->last_seq)->sortBy('seq');
            $rows = [];
            foreach ($events as $e) {
                $at = CarbonImmutable::createFromTimestampMs($e['ts'] + $offset);
                $user = is_string($e['data']['user'] ?? null) ? mb_substr($e['data']['user'], 0, 64) : null;
                $rows[] = [
                    'pc_id' => $pc->id, 'agent_id' => $agent->id, 'seq' => $e['seq'], 'lab_session_id' => $session?->id,
                    'type' => $e['type'], 'occurred_at' => $at, 'received_at' => now(), 'user' => $user,
                    'data' => json_encode($e['data'], JSON_UNESCAPED_SLASHES),
                ];
            }
            foreach (array_chunk($rows, 200) as $chunk) {
                Event::query()->insertOrIgnore($chunk);
            }
            foreach ($events as $e) {
                $at = CarbonImmutable::createFromTimestampMs($e['ts'] + $offset);
                if ($a = $this->derive($pc, $session, $e['type'], $e['data'], $at)) {
                    $alerts[] = $a;
                }
            }
            if ($events->isNotEmpty()) {
                $agent->last_seq = max($agent->last_seq, (int) $events->max('seq'));
            }
            $agent->save();
            if ($alerts) {
                $this->refreshLiveScore($pc, $session);
            }
            $pc->save();
        });

        foreach ($alerts as $a) {
            broadcast(new AlertRaised($a));
        }
        if (json_encode($pc->tile()) !== $tileBefore) {
            broadcast(new PcUpdated($pc));
        }

        return [
            'ack_seq' => $agent->last_seq,
            'server_time_ms' => now()->getTimestampMs(),
            'policy_version' => $this->policies->forPc($pc)['version'],
            'signatures_version' => SignatureVersion::active()?->version ?? '',
            'isolated' => $pc->isolated,
        ];
    }

    private function validate(Agent $agent, array $b): void
    {
        $fail = fn (string $m) => throw ValidationException::withMessages(['batch' => $m]);
        if (($b['v'] ?? null) !== 1) {
            $fail('unsupported protocol version');
        }
        if ((string) ($b['pc_id'] ?? '') !== (string) $agent->pc_id) {
            $fail('pc_id does not match the authenticated agent');
        }
        $events = $b['events'] ?? [];
        if (! is_array($events) || count($events) > config('xephyguard.max_events_per_batch')) {
            $fail('events must be an array of at most '.config('xephyguard.max_events_per_batch'));
        }
        if (isset($b['heartbeat']) && ! is_array($b['heartbeat'])) {
            $fail('heartbeat must be an object');
        }
        foreach ($events as $i => $e) {
            if (! is_array($e) || ! is_int($e['seq'] ?? null) || $e['seq'] < 1 || ! is_int($e['ts'] ?? null)
                || ! in_array($e['type'] ?? null, self::TYPES, true) || ! is_array($e['data'] ?? null)) {
                $fail("event $i is malformed");
            }
            if (strlen(json_encode($e['data'])) > 64 * 1024) {
                $fail("event $i is too large");
            }
        }
    }

    private function applyHeartbeat(Agent $agent, Pc $pc, array $hb, string $ip, ?LabSession $session): void
    {
        $pc->last_seen_at = now();
        $pc->ip = $ip;
        $pc->silent_alerted_at = null;
        $pc->clean_shutdown = false;
        if (! $hb) {
            return;
        }
        $pc->mode = is_string($hb['mode'] ?? null) ? $hb['mode'] : $pc->mode;
        $pc->current_user = is_string($hb['user'] ?? null) && $hb['user'] !== '' ? mb_substr($hb['user'], 0, 64) : null;
        $pc->capabilities = is_array($hb['capabilities'] ?? null) ? $hb['capabilities'] : $pc->capabilities;
        $pc->usage = is_array($hb['usage'] ?? null) ? $hb['usage'] + ['helpers' => $hb['helpers'] ?? null,
            'queue_depth' => $hb['queue_depth'] ?? null, 'realtime' => $hb['realtime'] ?? null] : $pc->usage;
        if ($pc->status === 'offline') {
            $pc->status = 'clean';
        }
        $agent->version = is_string($hb['agent_version'] ?? null) ? $hb['agent_version'] : $agent->version;
        $agent->policy_version = isset($hb['policy_version']) ? (string) $hb['policy_version'] : $agent->policy_version;
        $agent->signatures_version = is_string($hb['signatures_version'] ?? null) ? $hb['signatures_version'] : $agent->signatures_version;

        if ($session && $pc->current_user && ($student = Student::forLogin($pc->current_user))) {
            $a = StudentPcAssignment::query()->firstOrNew(['student_id' => $student->id, 'pc_id' => $pc->id,
                'lab_session_id' => $session->id]);
            $a->first_seen_at ??= now();
            $a->last_seen_at = now();
            $a->save();
            SessionPc::query()->where('lab_session_id', $session->id)->where('pc_id', $pc->id)->update(['student_id' => $student->id]);
        }
    }

    /** Creates derived rows; returns an Alert when one was raised. */
    private function derive(Pc $pc, ?LabSession $session, string $type, array $d, CarbonImmutable $at): ?Alert
    {
        $sid = $session?->id;
        $str = fn (string $k, int $n = 64) => is_scalar($d[$k] ?? null) ? mb_substr((string) $d[$k], 0, $n) : null;
        $int = fn (string $k) => is_numeric($d[$k] ?? null) ? (int) $d[$k] : 0;

        switch ($type) {
            case 'rule_hit':
                $actions = is_array($d['actions'] ?? null) ? $d['actions'] : [];
                $pc->last_event = mb_substr(($d['description'] ?? $d['rule_id'] ?? 'rule hit').' ('.($d['subject'] ?? '').')', 0, 160);
                if (in_array('isolate', $actions, true) && ! $pc->isolated) {
                    $this->sessions->isolate($pc, true, null, 'rule '.$str('rule_id'));
                }
                if (! in_array('alert', $actions, true)) {
                    return null;
                }
                $title = ($d['description'] ?? 'Rule matched').': '.($str('subject', 120) ?? '');
                // An agent restart resets its local de-duplication; do not raise the same alert twice.
                $dup = Alert::query()->where('pc_id', $pc->id)->where('lab_session_id', $sid)
                    ->where('rule_id', $str('rule_id') ?? 'rule')->where('title', mb_substr($title, 0, 200))
                    ->where('occurred_at', '>', $at->subMinutes(30))->exists();
                if ($dup) {
                    return null;
                }

                return $this->alert($pc, $sid, $str('rule_id') ?? 'rule', $str('severity', 16) ?? 'medium',
                    $str('category', 32), ($d['description'] ?? 'Rule matched').': '.($str('subject', 120) ?? ''), $d, $at, $str('user'));

            case 'tamper':
                $kind = $str('kind', 48) ?? 'unknown';
                $pc->last_event = 'Tamper: '.$kind;

                return $this->alert($pc, $sid, 'tamper.'.$kind, $kind === 'clock_change' || $kind === 'config_changed' ? 'high' : 'critical',
                    'tamper', self::TAMPER_TITLES[$kind] ?? "Tamper: $kind", $d, $at, $str('user'));

            case 'agent_started':
                $pc->last_event = 'Agent started';
                if (($d['previous_exit'] ?? '') === 'unclean') {
                    return $this->alert($pc, $sid, 'tamper.unclean_restart', $sid ? 'high' : 'medium', 'tamper',
                        'Agent restarted after an unclean exit (killed, crashed or power loss)', $d, $at, null);
                }

                return null;

            case 'agent_stopping':
                $pc->clean_shutdown = true;
                $pc->last_event = 'Agent stopped ('.($str('reason', 32) ?? 'stop').')';
                if ($sid) {
                    return $this->alert($pc, $sid, 'tamper.agent_stopped', 'high', 'tamper',
                        'Agent service was stopped during the session', $d, $at, null);
                }

                return null;

            case 'key_stats':
                KeyStat::create([
                    'pc_id' => $pc->id, 'lab_session_id' => $sid, 'user' => $str('user'),
                    'window_start' => CarbonImmutable::createFromTimestampMs($int('window_start') ?: $at->getTimestampMs()),
                    'window_s' => $int('window_s') ?: 5, 'keys' => $int('keys'), 'backspaces' => $int('backspaces'),
                    'iki_mean' => $d['iki_mean'] ?? null, 'iki_median' => $d['iki_median'] ?? null, 'iki_p90' => $d['iki_p90'] ?? null,
                    'iki_std' => $d['iki_std'] ?? null, 'bursts' => $int('bursts'), 'max_pause' => $int('max_pause') ?: null,
                    'paste_chords' => $int('paste_chords'),
                ]);

                return null;

            case 'paste':
            case 'copy':
                PasteEvent::create([
                    'pc_id' => $pc->id, 'lab_session_id' => $sid, 'user' => $str('user'), 'is_copy' => $type === 'copy',
                    'chars' => $int('chars'), 'lines' => $int('lines'), 'looks_like_code' => (bool) ($d['code'] ?? false),
                    'code_score' => (float) ($d['score'] ?? 0), 'source_app' => $str('src'), 'target_app' => $str('dst'),
                    'occurred_at' => $at,
                ]);
                $threshold = (int) Setting::get('paste_alert_chars', 800);
                if ($type === 'paste' && $sid && $session->mode === 'test' && $int('chars') >= $threshold) {
                    $pc->last_event = "Large paste: {$int('chars')} chars";

                    return $this->alert($pc, $sid, 'behavior.large_paste', 'medium', 'behavior',
                        sprintf('Large paste: %d characters, %d lines%s', $int('chars'), $int('lines'),
                            ($d['code'] ?? false) ? ' (looks like code)' : ''), $d, $at, $str('user'));
                }

                return null;

            case 'window_focus':
                $pc->active_app = $str('app');

                return null;

            case 'command_result':
                if (is_numeric($d['command_id'] ?? null)) {
                    Command::query()->whereKey((int) $d['command_id'])->where('pc_id', $pc->id)->update([
                        'status' => in_array($d['status'] ?? '', ['ok', 'failed', 'ignored'], true) ? $d['status'] : 'failed',
                        'result' => json_encode($d['detail'] ?? null), 'acked_at' => now(),
                    ]);
                }

                return null;

            case 'net_adapter':
                $pc->last_event = mb_substr('Network: '.($str('change', 16) ?? '').' '.($str('name') ?? ''), 0, 160);

                return null;

            case 'device':
                $pc->last_event = mb_substr('Device '.($str('action', 8) ?? '').': '.($str('model', 80) ?: $str('class', 32)), 0, 160);

                return null;

            case 'helper_status':
                if (($d['connected'] ?? true) === false && $sid) {
                    $pc->last_event = 'Session helper disconnected';
                }

                return null;
        }

        return null;
    }

    private function alert(Pc $pc, ?int $sid, string $rule, string $severity, ?string $category, string $title,
        array $details, CarbonImmutable $at, ?string $user): Alert
    {
        $severity = array_key_exists($severity, Alert::SEVERITY_POINTS) ? $severity : 'medium';
        $alert = Alert::create([
            'pc_id' => $pc->id, 'lab_session_id' => $sid, 'rule_id' => $rule, 'severity' => $severity,
            'category' => $category, 'title' => mb_substr($title, 0, 200), 'details' => $details,
            'user' => $user ?? $pc->current_user, 'occurred_at' => $at,
        ]);
        $alert->setRelation('pc', $pc);

        return $alert;
    }

    /** Live tile score: severity points of this session's alerts (capped). Not the integrity score. */
    public function refreshLiveScore(Pc $pc, ?LabSession $session): void
    {
        $q = Alert::query()->where('pc_id', $pc->id);
        $session ? $q->where('lab_session_id', $session->id) : $q->where('occurred_at', '>', now()->subHours(2));
        $alerts = $q->get(['severity', 'acknowledged_at']);
        $points = $alerts->sum(fn ($a) => Alert::SEVERITY_POINTS[$a->severity] ?? 0);
        $pc->live_score = min(100, $points);
        $open = $alerts->whereNull('acknowledged_at');
        $pc->status = $open->whereIn('severity', ['high', 'critical'])->isNotEmpty() ? 'alert'
            : ($open->where('severity', 'medium')->isNotEmpty() ? 'warning' : 'clean');
    }
}
