<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\ResolvedPolicy;
use App\Models\Rule;
use App\Models\SessionPc;
use App\Models\Setting;
use App\Models\SignatureVersion;

/** Resolves the effective policy for one PC: active session -> profile (or per-PC override). */
class PolicyService
{
    /** @return array{profile: Profile, session: ?LabSession, override: ?SessionPc} */
    public function effective(Pc $pc): array
    {
        return $this->effectiveMany([$pc])[$pc->id];
    }

    /**
     * effective() for many PCs with a fixed query count (S-24): active sessions of their labs, the
     * session profiles, the PC links with their override profiles, and the Always-on profile once.
     *
     * @param  iterable<Pc>  $pcs
     * @return array<int, array{profile: Profile, session: ?LabSession, override: ?SessionPc}>
     */
    public function effectiveMany(iterable $pcs): array
    {
        $pcs = collect($pcs);
        $sessions = [];   // lab_id => latest active session (same pick as LabSession::activeFor)
        $labIds = $pcs->pluck('lab_id')->unique()->filter()->values();
        if ($labIds->isNotEmpty()) {
            foreach (LabSession::query()->with('profile')->whereIn('lab_id', $labIds)->active()
                ->orderByDesc('started_at')->orderByDesc('id')->get() as $s) {
                $sessions[$s->lab_id] ??= $s;
            }
        }
        $links = [];      // "session_id:pc_id" => link
        if ($sessions !== []) {
            foreach (SessionPc::query()->with('overrideProfile')
                ->whereIn('lab_session_id', array_map(fn ($s) => $s->id, array_values($sessions)))
                ->whereIn('pc_id', $pcs->pluck('id'))->get() as $l) {
                $links[$l->lab_session_id.':'.$l->pc_id] ??= $l;
            }
        }
        $alwaysOn = null;
        $out = [];
        foreach ($pcs as $pc) {
            $session = $sessions[$pc->lab_id] ?? null;
            $link = $session ? ($links[$session->id.':'.$pc->id] ?? null) : null;
            if ($session && $link) {
                $out[$pc->id] = ['profile' => $link->overrideProfile ?? $session->profile, 'session' => $session,
                    'override' => $link->override_profile_id ? $link : null];

                continue;
            }
            $out[$pc->id] = ['profile' => $alwaysOn ??= self::alwaysOnProfile(), 'session' => null, 'override' => null];
        }

        return $out;
    }

    public static function alwaysOnProfile(): Profile
    {
        return Profile::query()->where('mode', 'always_on')->orderByDesc('builtin')->orderBy('id')->first()
            ?? new Profile(['name' => 'Always-on', 'mode' => 'always_on', 'network' => [], 'agent' => []]);
    }

    /**
     * The policy the agent gets, from the per-PC cache when still valid (S-23). Valid = same global
     * generation (bumped by every profile/rule/session/override/setting/signature change) and same
     * config + privacy-rules file stamp. A hit costs two small queries.
     */
    public function forPc(Pc $pc): array
    {
        $generation = ResolvedPolicy::generation();   // read first: a change during resolve() makes the row stale
        $stamp = $this->stamp();
        $row = ResolvedPolicy::query()->where('pc_id', $pc->id)->first();
        if ($row && $row->generation === $generation && hash_equals($row->stamp, $stamp)) {
            return $row->policy;
        }
        $policy = $this->resolve($pc);
        ResolvedPolicy::query()->updateOrCreate(['pc_id' => $pc->id],
            ['generation' => $generation, 'stamp' => $stamp, 'version' => $policy['version'], 'policy' => $policy]);

        return $policy;
    }

    /** Inputs that are not DB rows: config/env values (Setting falls back to them), notice features, the privacy-rules file. */
    private function stamp(): string
    {
        $rules = config('xephyguard.data_dir').'/screen/privacy_rules.yaml';

        return hash('sha256', json_encode(config('xephyguard')).'|'.json_encode(NoticeText::features()).'|'
            .(is_file($rules) ? (string) filemtime($rules) : '-'));
    }

    /** Resolves the policy from scratch (no cache). */
    public function resolve(Pc $pc): array
    {
        ['profile' => $profile, 'session' => $session] = $this->effective($pc);
        $mode = $profile->mode;
        $agent = $profile->agent ?? [];
        $proxyOnly = Setting::get('network_mode', config('xephyguard.network_mode', 'gateway')) === 'proxy_only';

        $ruleActions = [];
        $disabled = [];
        foreach (Rule::query()->get(['rule_id', 'enabled', 'actions']) as $r) {
            if (! $r->enabled) {
                $disabled[] = $r->rule_id;
            }
            if (! empty($r->actions[$mode])) {
                $ruleActions[$r->rule_id] = array_values($r->actions[$mode]);
            }
        }
        $ruleActions = array_merge($ruleActions, $agent['rule_actions'] ?? []);

        $policy = array_filter([
            'mode' => $mode,
            'session_id' => $session?->id ?? 0,
            'profile' => $profile->name,
            'title_capture' => PrivacySettings::effectiveTitleCapture($mode, $agent['title_capture'] ?? null),
            'collect' => array_merge(['window' => true, 'clipboard' => true, 'keys' => true], $agent['collect'] ?? []),
            'intervals' => $agent['intervals'] ?? null,
            'actions' => array_filter(['default' => $agent['default_actions'] ?? null, 'rules' => $ruleActions ?: null]),
            'disabled_rules' => $disabled,
            'work_dirs' => $agent['work_dirs'] ?? Setting::get('work_dirs', ['~/lab', '~/Documents/Lab', '%USERPROFILE%\\Documents\\Lab']),
            'kill_exempt' => $agent['kill_exempt'] ?? [],
            'signatures_version' => SignatureVersion::active()?->version ?? '',
            'notice_text' => NoticeText::text(),   // screen sentences generated from the enabled features (S-25)
            // cpu_budget_pct only once an admin has set it; the agent's default is 8 (docs/PROTOCOL.md).
            'screen' => array_filter([
                'privacy_rules' => app(ScreenPrivacyRules::class)->rules(),
                'cpu_budget_pct' => ($b = Setting::get('screen.cpu_budget_pct')) !== null ? (int) $b : null,
            ], fn ($v) => $v !== null && $v !== []),
            'suppress_ai_overview' => (bool) ($profile->network['google_udm14'] ?? false),
            'google_search_url' => Setting::get('google_search_url'),
            'proxy_mode' => Setting::get('network_mode', config('xephyguard.network_mode', 'gateway')),
            'proxy_pac_url' => (Setting::get('network_mode', config('xephyguard.network_mode', 'gateway')) === 'proxy_only' && Setting::get('ccc_ip'))
                ? 'https://'.Setting::get('ccc_ip').'/proxy.pac'  // lab CA: a LAN host cannot swap the PAC (F97)
                : null,
            // Session egress lock on the PC; only meaningful without a gateway, so gateway policies are unchanged.
            'network_lock' => ($proxyOnly && $session && $mode !== 'always_on')
                ? (in_array($session->network_lock, LabSession::NETWORK_LOCKS, true) ? $session->network_lock : 'observe')
                : null,
        ], fn ($v) => $v !== null && $v !== []);
        $policy['session_id'] = $session?->id ?? 0;
        $policy['version'] = self::version($policy);

        return $policy;
    }

    /** Deterministic policy version: agents compare it with the value in every batch ack. */
    public static function version(array $policy): int
    {
        unset($policy['version']);
        ksort($policy);

        return (int) sprintf('%u', crc32(json_encode($policy)));
    }

    /** Full response of GET /api/agent/v1/policy. */
    public function response(Pc $pc): array
    {
        $sig = SignatureVersion::active();

        return [
            'policy' => $this->forPc($pc),
            'signatures' => $sig?->envelope(),
            'realtime' => [
                'app_key' => config('reverb.apps.apps.0.key'),
                'port' => config('xephyguard.agent_port'),
                'channel' => 'private-pc.'.$pc->id,
                'auth_path' => '/api/agent/v1/broadcasting/auth',
            ],
        ];
    }
}
