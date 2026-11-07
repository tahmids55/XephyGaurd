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
use App\Models\NetEvent;
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
        'command_result', 'helper_status', 'ide_ai_activity', 'isolation_lease_expired',
        'net_block', 'dns_block', 'sni_block', 'local_model'];

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

    public function __construct(
        private readonly SessionManager $sessions,
        private readonly PolicyBuilder $policies,
        private readonly CommandBus $commands,
    ) {}

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
            $aiPolicy = $session ? $this->policies->effectiveAiPolicy($pc) : 'open';
            $rows = [];
            foreach ($events as $e) {
                $at = CarbonImmutable::createFromTimestampMs($e['ts'] + $offset);
                $user = is_string($e['data']['user'] ?? null) ? mb_substr($e['data']['user'], 0, 64) : null;
                $rawData = $e['data'];
                if ($aiPolicy === 'permitted_with_disclosure' && self::isAiToolEvent($e['type'], $rawData)) {
                    $rawData['ai_policy'] = 'permitted_with_disclosure';
                }
                $enrichedData = self::withEvidenceMeta($e['type'], $rawData);
                $rows[] = [
                    'pc_id' => $pc->id, 'agent_id' => $agent->id, 'seq' => $e['seq'], 'lab_session_id' => $session?->id,
                    'type' => $e['type'], 'occurred_at' => $at, 'received_at' => now(), 'user' => $user,
                    'data' => json_encode($enrichedData, JSON_UNESCAPED_SLASHES),
                ];
            }
            foreach (array_chunk($rows, 200) as $chunk) {
                Event::query()->insertOrIgnore($chunk);
            }
            foreach ($events as $e) {
                $at = CarbonImmutable::createFromTimestampMs($e['ts'] + $offset);
                $rawData = $e['data'];
                if ($aiPolicy === 'permitted_with_disclosure' && self::isAiToolEvent($e['type'], $rawData)) {
                    $rawData['ai_policy'] = 'permitted_with_disclosure';
                }
                if ($a = $this->derive($pc, $session, $e['type'], self::withEvidenceMeta($e['type'], $rawData), $at)) {
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

        // Re-deliver any recent unacknowledged commands (e.g. if sent while WebSocket was reconnecting).
        $pendingCmds = Command::query()
            ->where('pc_id', $pc->id)
            ->where('status', 'sent')
            ->whereNull('acked_at')
            ->where('created_at', '>', now()->subMinutes(10))
            ->where('created_at', '<', now()->subSeconds(10))
            ->get();
        foreach ($pendingCmds as $cmd) {
            broadcast(new \App\Events\AgentCommand($cmd));
        }

        return [
            'pc_id' => $agent->pc_id,
            'ack_seq' => $agent->last_seq,
            'server_time_ms' => now()->getTimestampMs(),
            'policy_version' => $this->policies->forPc($pc)['version'],
            'signatures_version' => SignatureVersion::active()?->version ?? '',
            'isolated' => $pc->isolated,
            'isolation_lease_s' => (int) config('xephyguard.isolation_lease_seconds', 600),
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
                $sev = $str('severity', 16) ?? 'medium';
                $ruleId = $str('rule_id') ?? 'rule';
                $cat = $str('category', 32) ?? '';
                $desc = (string) ($d['description'] ?? $ruleId);
                $subject = $str('subject', 120) ?? '';
                $mode = $session?->mode ?? 'always_on';
                $isInventory = str_starts_with($ruleId, 'ext.')
                    || str_starts_with($ruleId, 'artifact.')
                    || in_array($cat, ['extension', 'artifact'], true)
                    || ($d['evidence'] ?? '') === 'extension_installed';

                if (($d['event_class'] ?? '') === 'disclosure' || (($d['ai_policy'] ?? '') === 'permitted_with_disclosure' && self::isAiToolEvent($type, $d))) {
                    $pc->last_event = mb_substr('Disclosed AI: '.$desc.' ('.$subject.')', 0, 160);

                    return null;
                }

                $pc->last_event = mb_substr($desc.' ('.$subject.')', 0, 160);

                // C4 / R6: Isolate and kill only in active Test sessions when the profile action includes it.
                if ($mode === 'test' && ! $isInventory && in_array('isolate', $actions, true) && ! $pc->isolated) {
                    $this->sessions->isolate($pc, true, null, 'rule '.$ruleId);
                }
                if ($mode === 'test' && ! $isInventory && in_array('kill', $actions, true) && $this->profileActionIncludesKill($pc, $ruleId, $sev)) {
                    $browsers = ['chrome', 'msedge', 'firefox', 'brave', 'opera', 'vivaldi', 'arc'];
                    if ($subject !== '' && ! in_array(mb_strtolower($subject), $browsers, true)) {
                        $this->commands->send($pc, 'kill', ['exe' => $subject]);
                    }
                }

                // C4 / R5: Popups only in Test, or for active use in Practice when blocking AI; never for inventory or always_on.
                $canWarn = ! $isInventory && in_array('warn', $actions, true) && (
                    $mode === 'test' || ($mode === 'practice' && $this->profileBlocksAi($pc, $session))
                );
                if ($canWarn) {
                    $recentWarn = Command::query()
                        ->where('pc_id', $pc->id)
                        ->where('type', 'message')
                        ->where('created_at', '>', now()->subSeconds(30))
                        ->exists();
                    if (! $recentWarn) {
                        $this->commands->send($pc, 'message', [
                            'title' => 'XephyGuard Notice — AI Tool Activity',
                            'text' => "{$desc} was detected on this PC and logged for your instructor.",
                            'urgent' => $mode === 'test',
                        ]);
                    }
                }

                if ($mode === 'always_on' || ! in_array('alert', $actions, true)) {
                    return null;
                }

                $title = mb_substr(($d['description'] ?? 'Rule matched').': '.$subject, 0, 200);
                // R5: Installed-only inventory items are reported at most once per session / 24h per subject.
                if ($isInventory) {
                    $dup = Alert::query()
                        ->where('pc_id', $pc->id)
                        ->where('rule_id', $ruleId)
                        ->where('title', $title)
                        ->where(function ($q) use ($sid, $at) {
                            $q->where('occurred_at', '>', $at->subHours(24));
                            if ($sid !== null) {
                                $q->orWhere('lab_session_id', $sid);
                            }
                        })
                        ->exists();
                } else {
                    // An agent restart resets its local de-duplication; do not raise the same alert twice.
                    $dup = Alert::query()
                        ->where('pc_id', $pc->id)
                        ->where('lab_session_id', $sid)
                        ->where('rule_id', $ruleId)
                        ->where('title', $title)
                        ->where('occurred_at', '>', $at->subMinutes(30))
                        ->exists();
                }
                if ($dup) {
                    return null;
                }

                return $this->alert($pc, $sid, $ruleId, $sev,
                    $cat ?: null, $title, $d, $at, $str('user'));

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

            case 'process_snapshot':
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
                $app = $str('app') ?? '';
                $winTitle = $str('title', 200) ?? '';
                $pc->active_app = $app;

                // R7 / C4: Window-title keyword match is weak corroborating evidence only:
                // logged event + optional low-severity alert in Test only; never kill, never isolate, no popup.
                if ($winTitle !== '' && mb_strtolower($winTitle) !== mb_strtolower($app) && ($match = $this->matchWindowAi($app, $winTitle))) {
                    $pc->last_event = mb_substr("Window title keyword (weak): {$match['name']} ({$winTitle})", 0, 160);
                    $aiPolicy = $session ? $this->policies->effectiveAiPolicy($pc) : 'open';
                    if ($session && $session->mode === 'test' && $aiPolicy === 'forbidden' && config('xephyguard.window_title_alerts', true)) {
                        $alertTitle = mb_substr("Window title keyword (weak): {$match['name']} in {$app} ({$winTitle})", 0, 200);
                        $dup = Alert::query()
                            ->where('pc_id', $pc->id)
                            ->where('lab_session_id', $sid)
                            ->where('rule_id', $match['id'])
                            ->where('occurred_at', '>', $at->subMinutes(5))
                            ->exists();
                        if (! $dup) {
                            return $this->alert($pc, $sid, $match['id'], 'low', 'web_ai', $alertTitle, $d + $match, $at, $str('user'));
                        }
                    }
                }

                return null;

            case 'ide_ai_activity':
                // C2: Truthful IDE AI activity signal (file_activity, process, extension_installed).
                // Agent-side signals are at most medium confidence; alert only during a Test session.
                $tool = $str('tool', 64) ?? 'ide-ai';
                $ide = $str('ide', 32) ?? 'ide';
                $evidence = $str('evidence', 32) ?? 'file_activity';
                $confidence = ($d['confidence'] ?? 'medium') === 'weak' ? 'weak' : 'medium';
                $detail = $str('detail', 200);
                $meaning = $str('meaning', 255) ?? 'IDE AI extension activity was observed';
                $notMeaning = $str('not_meaning', 255) ?? 'Does not prove chat use; inline completions, sign-in or telemetry produce the same signal';

                $pc->last_event = mb_substr("IDE AI ({$confidence}): {$tool} in {$ide} [{$evidence}]", 0, 160);

                if (($d['event_class'] ?? '') === 'disclosure' || ($d['ai_policy'] ?? '') === 'permitted_with_disclosure') {
                    return null;
                }

                if (! $session || $session->mode !== 'test') {
                    return null;
                }

                $ruleId = 'ide.ai.'.preg_replace('/[^a-z0-9_.-]/', '_', strtolower($tool));
                $sev = ($confidence === 'weak' || $evidence === 'extension_installed') ? 'low' : 'medium';
                $title = mb_substr("IDE AI ({$confidence}): {$tool} in {$ide}".($detail ? " ({$detail})" : ''), 0, 200);
                $dedupSince = $evidence === 'extension_installed' ? $at->subHours(24) : $at->subMinutes(10);
                $dup = Alert::query()
                    ->where('pc_id', $pc->id)
                    ->where('lab_session_id', $sid)
                    ->where('rule_id', $ruleId)
                    ->where('occurred_at', '>', $dedupSince)
                    ->exists();
                if ($dup) {
                    return null;
                }

                return $this->alert($pc, $sid, $ruleId, $sev, 'ide_ai', $title, array_merge($d, [
                    'tool' => $tool,
                    'ide' => $ide,
                    'evidence' => $evidence,
                    'detail' => $detail,
                    'confidence' => $confidence,
                    'meaning' => $meaning,
                    'not_meaning' => $notMeaning,
                ]), $at, $str('user'));

            case 'isolation_lease_expired':
                $pc->isolated = false;
                $pc->last_event = 'Isolation lease expired (network restored)';

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

            case 'net_block':
            case 'dns_block':
            case 'sni_block':
                $host = $str('host', 120) ?? $str('subject', 120) ?? 'domain';
                $pc->last_event = mb_substr("Blocked ({$type}): {$host}", 0, 160);

                return null;
        }

        return null;
    }

    public static function isAiToolEvent(string $type, array $d = []): bool
    {
        if (in_array($type, ['ide_ai_activity', 'extension_scan', 'local_model'], true)) {
            return true;
        }
        $ruleId = strtolower((string) ($d['rule_id'] ?? $d['id'] ?? ''));
        $cat = strtolower((string) ($d['category'] ?? ''));
        if ($type === 'rule_hit') {
            if (str_starts_with($ruleId, 'tamper.') || $cat === 'tamper') {
                return false;
            }
            if (
                str_starts_with($ruleId, 'proc.')
                || str_starts_with($ruleId, 'ide.')
                || str_starts_with($ruleId, 'ext.')
                || str_starts_with($ruleId, 'artifact.')
                || str_starts_with($ruleId, 'port.')
                || str_starts_with($ruleId, 'web.')
                || in_array($cat, ['ai', 'ide', 'ide_ai', 'local_llm', 'extension', 'artifact', 'web_ai'], true)
            ) {
                return true;
            }
        }

        return false;
    }

    /**
     * Classifies an event or alert into the Phase 1–4 evidence model (P1-C3, P4-C1):
     * - event_class: 'installed' | 'active' | 'blocked' | 'behavioral' | 'system' | 'disclosure'
     * - confidence: 'strong' | 'medium' | 'weak'
     * - what_it_means: string
     * - what_it_does_not_mean: string
     *
     * @return array{event_class: string, confidence: string, what_it_means: string, what_it_does_not_mean: string}
     */
    public static function classifyEvidence(string $type, array $d = []): array
    {
        $ruleId = strtolower((string) ($d['rule_id'] ?? $d['id'] ?? ''));
        $cat = strtolower((string) ($d['category'] ?? ''));
        $evidence = strtolower((string) ($d['evidence'] ?? ''));
        $explicitConf = in_array($d['confidence'] ?? null, ['strong', 'medium', 'weak'], true) ? $d['confidence'] : null;

        // 0. AI-Permitted with Disclosure Mode (P4-C1)
        if ((($d['ai_policy'] ?? '') === 'permitted_with_disclosure' || ($d['event_class'] ?? '') === 'disclosure') && self::isAiToolEvent($type, $d)) {
            return [
                'event_class' => 'disclosure',
                'confidence' => $explicitConf ?? 'medium',
                'what_it_means' => (string) ($d['what_it_means'] ?? $d['meaning'] ?? 'AI tool activity logged transparently under AI-Permitted with Disclosure policy'),
                'what_it_does_not_mean' => (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? 'Not a rule violation; AI assistance is permitted with disclosure in this session'),
            ];
        }

        // 1. Blocked network/DNS/SNI attempts
        if (
            in_array($type, ['net_block', 'dns_block', 'sni_block'], true)
            || in_array($cat, ['net_block', 'dns_block', 'sni_block', 'blocked'], true)
            || str_starts_with($ruleId, 'net_block')
            || str_starts_with($ruleId, 'dns_block')
            || str_starts_with($ruleId, 'sni_block')
            || (($d['action'] ?? '') === 'blocked' && isset($d['host']))
        ) {
            $confidence = $explicitConf ?? 'strong';
            $means = (string) ($d['what_it_means'] ?? $d['meaning'] ?? 'A network connection or DNS query to a restricted domain was intercepted and blocked');
            $notMeans = (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? 'The request was blocked before completion; background tabs, extensions or prefetchers can also trigger blocked lookups');

            return [
                'event_class' => 'blocked',
                'confidence' => $confidence,
                'what_it_means' => $means,
                'what_it_does_not_mean' => $notMeans,
            ];
        }

        // 2. Installed inventory (extensions, model files on disk, static artifacts)
        if (
            in_array($type, ['extension_scan', 'artifact_scan', 'local_model'], true)
            || ($type === 'ide_ai_activity' && $evidence === 'extension_installed')
            || str_starts_with($ruleId, 'ext.')
            || str_starts_with($ruleId, 'artifact.')
            || str_starts_with($ruleId, 'local_model')
            || in_array($cat, ['extension', 'artifact', 'local_model'], true)
            || $evidence === 'extension_installed'
        ) {
            $confidence = $explicitConf ?? 'weak';
            $means = (string) ($d['what_it_means'] ?? $d['meaning'] ?? 'An AI extension, tool or model file is installed on disk in the user profile');
            $notMeans = (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? 'Presence on disk does not prove the extension or model was actively used during this session');

            return [
                'event_class' => 'installed',
                'confidence' => $confidence,
                'what_it_means' => $means,
                'what_it_does_not_mean' => $notMeans,
            ];
        }

        // 3. Behavioral signals (paste, copy, key_stats, window_focus keyword match)
        if (
            in_array($type, ['paste', 'copy', 'key_stats', 'keystats', 'window_focus'], true)
            || str_starts_with($ruleId, 'behavior.')
            || str_starts_with($ruleId, 'web.')
            || in_array($cat, ['behavior', 'web_ai'], true)
        ) {
            $confidence = $explicitConf ?? 'weak';
            $defaultMeans = $type === 'window_focus' || str_starts_with($ruleId, 'web.')
                ? 'Window title matched an AI-related keyword'
                : 'Typing rhythm or clipboard size/code-likeness statistic was recorded';
            $defaultNotMeans = $type === 'window_focus' || str_starts_with($ruleId, 'web.')
                ? 'Does not prove AI use; an article, video, document or search result with this word in the title produces the same signal'
                : 'No keystrokes or clipboard text are ever recorded; pasting assignment starter code or notes produces the same signal';

            return [
                'event_class' => 'behavioral',
                'confidence' => $confidence,
                'what_it_means' => (string) ($d['what_it_means'] ?? $d['meaning'] ?? $defaultMeans),
                'what_it_does_not_mean' => (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? $defaultNotMeans),
            ];
        }

        // 4. System & tamper events
        if (
            in_array($type, ['heartbeat', 'tamper', 'agent_started', 'agent_stopping', 'isolation_lease_expired', 'capabilities', 'command_result', 'helper_status', 'process_snapshot'], true)
            || str_starts_with($ruleId, 'tamper.')
            || $cat === 'tamper'
        ) {
            $confidence = $explicitConf ?? 'strong';
            $defaultMeans = ($type === 'tamper' || str_starts_with($ruleId, 'tamper.') || $cat === 'tamper')
                ? 'Agent integrity, service state or system clock anomaly was detected'
                : 'Agent system lifecycle or telemetry event was recorded';
            $defaultNotMeans = ($type === 'tamper' || str_starts_with($ruleId, 'tamper.') || $cat === 'tamper')
                ? 'Power loss, OS updates or hardware resets can also cause service stops or clock drift'
                : 'Routine operational telemetry, not evidence of misconduct';

            return [
                'event_class' => 'system',
                'confidence' => $confidence,
                'what_it_means' => (string) ($d['what_it_means'] ?? $d['meaning'] ?? $defaultMeans),
                'what_it_does_not_mean' => (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? $defaultNotMeans),
            ];
        }

        // 5. Active use (proc.*, port.*, ide_ai_activity file_activity/process, adapter.*, device.*, vm.*)
        $isIdeActivity = $type === 'ide_ai_activity' || str_starts_with($ruleId, 'ide.ai.') || $cat === 'ide_ai';
        $confidence = $explicitConf ?? ($isIdeActivity ? 'medium' : 'strong');
        $defaultMeans = $isIdeActivity
            ? 'IDE AI extension process or storage activity was observed during the session'
            : 'An AI program, local model server, network adapter or device rule was actively triggered on this PC';
        $defaultNotMeans = $isIdeActivity
            ? 'Does not prove chat use; inline completions, sign-in or telemetry produce the same signal'
            : 'Does not capture screen contents or prompt text; verify context with the student and session timeline';

        return [
            'event_class' => 'active',
            'confidence' => $confidence,
            'what_it_means' => (string) ($d['what_it_means'] ?? $d['meaning'] ?? $defaultMeans),
            'what_it_does_not_mean' => (string) ($d['what_it_does_not_mean'] ?? $d['not_meaning'] ?? $defaultNotMeans),
        ];
    }

    public static function withEvidenceMeta(string $type, array $d = []): array
    {
        $meta = self::classifyEvidence($type, $d);
        $d['evidence_meta'] = $meta;
        $d['event_class'] = $meta['event_class'];
        $d['confidence'] = $meta['confidence'];
        $d['what_it_means'] = $meta['what_it_means'];
        $d['what_it_does_not_mean'] = $meta['what_it_does_not_mean'];
        $d['meaning'] ??= $meta['what_it_means'];
        $d['not_meaning'] ??= $meta['what_it_does_not_mean'];

        return $d;
    }

    private function alert(Pc $pc, ?int $sid, string $rule, string $severity, ?string $category, string $title,
        array $details, CarbonImmutable $at, ?string $user): Alert
    {
        $severity = array_key_exists($severity, Alert::SEVERITY_POINTS) ? $severity : 'medium';
        $detailsWithRule = $details + ['rule_id' => $rule, 'category' => $category];
        $enrichedDetails = self::withEvidenceMeta('rule_hit', $detailsWithRule);
        $alert = Alert::create([
            'pc_id' => $pc->id, 'lab_session_id' => $sid, 'rule_id' => $rule, 'severity' => $severity,
            'category' => $category, 'title' => mb_substr($title, 0, 200), 'details' => $enrichedDetails,
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

    /**
     * Matches active window/browser titles against known AI web services & assistants (R7).
     * Treated strictly as weak, corroborating evidence (never kills or isolates; subject to allowlist).
     *
     * @return ?array{id: string, name: string, host: string, confidence: string, meaning: string, not_meaning: string}
     */
    private function matchWindowAi(string $app, string $title): ?array
    {
        $t = trim(mb_strtolower($title));
        if ($t === '' || $t === mb_strtolower(trim($app))) {
            return null;
        }
        foreach (config('xephyguard.window_title_allowlist', []) as $allowed) {
            $needle = trim(mb_strtolower((string) $allowed));
            if ($needle !== '' && str_contains($t, $needle)) {
                return null;
            }
        }
        $patterns = [
            ['keywords' => ['chatgpt', 'openai'], 'id' => 'web.ai.chatgpt', 'name' => 'ChatGPT', 'host' => 'chatgpt.com'],
            ['keywords' => ['claude', 'anthropic'], 'id' => 'web.ai.claude', 'name' => 'Claude AI', 'host' => 'claude.ai'],
            ['keywords' => ['gemini', 'google ai studio', 'aistudio.google', 'notebooklm'], 'id' => 'web.ai.gemini', 'name' => 'Google Gemini', 'host' => 'gemini.google.com'],
            ['keywords' => ['perplexity'], 'id' => 'web.ai.perplexity', 'name' => 'Perplexity AI', 'host' => 'www.perplexity.ai'],
            ['keywords' => ['deepseek'], 'id' => 'web.ai.deepseek', 'name' => 'DeepSeek AI', 'host' => 'chat.deepseek.com'],
            ['keywords' => ['github copilot', 'copilot chat', 'github.com/copilot'], 'id' => 'web.ai.github_copilot', 'name' => 'GitHub Copilot Chat', 'host' => 'githubcopilot.com'],
            ['keywords' => ['copilot'], 'id' => 'web.ai.copilot', 'name' => 'Copilot AI', 'host' => 'copilot.microsoft.com'],
            ['keywords' => ['grok'], 'id' => 'web.ai.grok', 'name' => 'Grok AI', 'host' => 'grok.com'],
            ['keywords' => ['blackbox ai', 'blackbox.ai'], 'id' => 'web.ai.blackbox', 'name' => 'Blackbox AI', 'host' => 'www.blackbox.ai'],
            ['keywords' => ['phind'], 'id' => 'web.ai.phind', 'name' => 'Phind AI', 'host' => 'www.phind.com'],
            ['keywords' => ['poe - ', ' - poe', 'poe.com'], 'id' => 'web.ai.poe', 'name' => 'Poe AI', 'host' => 'poe.com'],
            ['keywords' => ['huggingchat', 'huggingface'], 'id' => 'web.ai.huggingchat', 'name' => 'HuggingChat', 'host' => 'huggingface.co'],
            ['keywords' => ['v0 by vercel', 'v0.dev'], 'id' => 'web.ai.v0', 'name' => 'Vercel v0', 'host' => 'v0.dev'],
            ['keywords' => ['bolt.new'], 'id' => 'web.ai.bolt', 'name' => 'Bolt.new', 'host' => 'bolt.new'],
            ['keywords' => ['lovable.dev', 'lovable ai'], 'id' => 'web.ai.lovable', 'name' => 'Lovable AI', 'host' => 'lovable.dev'],
            ['keywords' => ['meta ai', 'meta.ai'], 'id' => 'web.ai.meta', 'name' => 'Meta AI', 'host' => 'www.meta.ai'],
            ['keywords' => ['mistral', 'le chat'], 'id' => 'web.ai.mistral', 'name' => 'Mistral Le Chat', 'host' => 'chat.mistral.ai'],
        ];
        $meta = [
            'confidence' => 'weak',
            'meaning' => 'Window title matched an AI-related keyword',
            'not_meaning' => 'Does not prove AI use; an article, video, document or search result with this word in the title produces the same signal',
        ];
        foreach ($patterns as $p) {
            foreach ($p['keywords'] as $kw) {
                if (str_contains($t, $kw)) {
                    return ['id' => $p['id'], 'name' => $p['name'], 'host' => $p['host']] + $meta;
                }
            }
        }

        // Also match any enabled custom domain in the Blocklist whose full domain appears in the window title.
        foreach (\App\Models\BlocklistEntry::query()->where('enabled', true)->where('kind', 'domain')->get(['category', 'value']) as $entry) {
            $domain = mb_strtolower($entry->value);
            if ($domain !== '' && str_contains($t, $domain)) {
                $stem = preg_replace('/\.(com|ai|org|net|io|dev|co|app|chat)$/i', '', $domain);

                return ['id' => 'web.'.$entry->category.'.'.$stem, 'name' => $entry->value, 'host' => $entry->value] + $meta;
            }
        }

        return null;
    }

    /**
     * Determines whether the active session profile blocks AI categories.
     */
    private function profileBlocksAi(Pc $pc, ?LabSession $session): bool
    {
        if (! $session) {
            return false;
        }
        if ($this->policies->effectiveAiPolicy($pc) !== 'forbidden') {
            return false;
        }
        ['profile' => $profile] = $this->policies->effective($pc);
        $cats = $profile->network['block_categories'] ?? [];

        return ! empty($cats) || $profile->mode === 'test';
    }

    /**
     * Checks whether the effective policy for this PC in Test mode permits `kill` for the given rule/severity.
     */
    private function profileActionIncludesKill(Pc $pc, string $ruleId, string $severity): bool
    {
        $policy = $this->policies->forPc($pc);
        if (($policy['mode'] ?? '') !== 'test') {
            return false;
        }
        $ruleActs = $policy['actions']['rules'][$ruleId] ?? null;
        if (is_array($ruleActs)) {
            return in_array('kill', $ruleActs, true);
        }
        $defActs = $policy['actions']['default'][$severity] ?? [];

        return is_array($defActs) && in_array('kill', $defActs, true);
    }
}
