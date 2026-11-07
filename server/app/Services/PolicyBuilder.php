<?php

namespace App\Services;

use App\Models\BlocklistEntry;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\Rule;
use App\Models\SessionPc;
use App\Models\Setting;
use App\Models\SignatureVersion;

/** Resolves the effective policy for one PC: active session -> profile (or per-PC override). */
class PolicyBuilder
{
    /**
     * Contract C1 tiered collection ceiling per mode (mirrors docs/ETHICS.md).
     * Profile values may only reduce collection, never exceed the ceiling for the mode.
     */
    public const TIER_COLLECT = [
        'always_on' => [
            'processes' => true,
            'ports' => true,
            'extensions' => true,
            'network' => true,
            'devices' => true,
            'artifacts' => true,
            'model_files' => true,
            'vm' => true,
            'adapters' => true,
            'usb' => true,
            'window' => false,
            'keys' => false,
            'keystats' => false,
            'clipboard' => false,
            'paste' => false,
            'files' => false,
            'gpu' => false,
        ],
        'practice' => [
            'processes' => true,
            'ports' => true,
            'extensions' => true,
            'network' => true,
            'devices' => true,
            'artifacts' => true,
            'model_files' => true,
            'vm' => true,
            'adapters' => true,
            'usb' => true,
            'window' => true,
            'keys' => true,
            'keystats' => true,
            'clipboard' => true,
            'paste' => true,
            'files' => false,
            'gpu' => false,
        ],
        'test' => [
            'processes' => true,
            'ports' => true,
            'extensions' => true,
            'network' => true,
            'devices' => true,
            'artifacts' => true,
            'model_files' => true,
            'vm' => true,
            'adapters' => true,
            'usb' => true,
            'window' => true,
            'keys' => true,
            'keystats' => true,
            'clipboard' => true,
            'paste' => true,
            'files' => true,
            'gpu' => true,
        ],
    ];

    private const COLLECT_ALIASES = [
        'keys' => ['keys', 'keystats'],
        'keystats' => ['keys', 'keystats'],
        'clipboard' => ['clipboard', 'paste'],
        'paste' => ['clipboard', 'paste'],
        'artifacts' => ['artifacts', 'model_files'],
        'model_files' => ['artifacts', 'model_files'],
        'network' => ['network', 'adapters', 'vm'],
        'adapters' => ['network', 'adapters'],
        'vm' => ['network', 'vm'],
        'devices' => ['devices', 'usb'],
        'usb' => ['devices', 'usb'],
    ];

    /** @return array{profile: Profile, session: ?LabSession, override: ?SessionPc} */
    public function effective(Pc $pc): array
    {
        $session = LabSession::activeFor($pc->lab_id);
        $link = $session ? SessionPc::query()->where('lab_session_id', $session->id)->where('pc_id', $pc->id)->first() : null;
        if ($session && $link) {
            $profile = $link->overrideProfile ?? $session->profile;

            return ['profile' => $profile, 'session' => $session, 'override' => $link->override_profile_id ? $link : null];
        }

        return ['profile' => self::alwaysOnProfile(), 'session' => null, 'override' => null];
    }

    public static function alwaysOnProfile(): Profile
    {
        return Profile::query()->where('mode', 'always_on')->orderByDesc('builtin')->orderBy('id')->first()
            ?? new Profile(['name' => 'Always-on', 'mode' => 'always_on', 'ai_policy' => 'open', 'network' => [], 'agent' => ['title_capture' => 'off']]);
    }

    public function effectiveAiPolicy(Pc $pc): string
    {
        ['profile' => $profile, 'session' => $session, 'override' => $override] = $this->effective($pc);

        return self::resolveAiPolicy($profile, $session, $override);
    }

    public static function resolveAiPolicy(Profile $profile, ?LabSession $session = null, ?SessionPc $override = null): string
    {
        if ($override?->overrideProfile) {
            $p = $override->overrideProfile->ai_policy;

            return in_array($p, Profile::AI_POLICIES, true) ? $p : 'forbidden';
        }
        if ($session) {
            if (in_array($session->ai_policy, ['permitted_with_disclosure', 'open'], true)) {
                return $session->ai_policy;
            }
            if (in_array($profile->ai_policy, ['permitted_with_disclosure', 'open'], true)) {
                return $profile->ai_policy;
            }

            return in_array($session->ai_policy, Profile::AI_POLICIES, true)
                ? $session->ai_policy
                : (in_array($profile->ai_policy, Profile::AI_POLICIES, true) ? $profile->ai_policy : 'forbidden');
        }
        if ($profile->mode === 'always_on') {
            return in_array($profile->ai_policy, Profile::AI_POLICIES, true) ? $profile->ai_policy : 'open';
        }

        return in_array($profile->ai_policy, Profile::AI_POLICIES, true) ? $profile->ai_policy : 'forbidden';
    }

    public static function clampTitleCapture(string $mode, ?string $requested): string
    {
        if ($mode === 'always_on') {
            return 'off';
        }
        if ($mode === 'practice') {
            return $requested === 'off' ? 'off' : 'app_only';
        }

        return in_array($requested, ['off', 'app_only', 'full'], true) ? $requested : 'app_only';
    }

    public static function clampCollect(string $mode, array $requested = []): array
    {
        $ceiling = self::TIER_COLLECT[$mode] ?? self::TIER_COLLECT['always_on'];
        $effectiveReq = [];
        foreach ($requested as $k => $v) {
            $boolVal = (bool) $v;
            $effectiveReq[$k] = isset($effectiveReq[$k]) ? ($effectiveReq[$k] && $boolVal) : $boolVal;
            foreach (self::COLLECT_ALIASES[$k] ?? [] as $alias) {
                $effectiveReq[$alias] = isset($effectiveReq[$alias]) ? ($effectiveReq[$alias] && $boolVal) : $boolVal;
            }
        }

        $out = [];
        foreach ($ceiling as $k => $maxAllowed) {
            $out[$k] = $maxAllowed && (bool) ($effectiveReq[$k] ?? true);
        }

        return $out;
    }

    public static function clampActionsForMode(string $mode, array $actions, bool $blocksAi = false, bool $isInventory = false): array
    {
        $allowed = match ($mode) {
            'always_on' => ['log'],
            'practice' => ($blocksAi && ! $isInventory) ? ['log', 'warn', 'alert'] : ['log', 'alert'],
            'test' => $isInventory ? ['log', 'alert'] : ['log', 'warn', 'kill', 'alert', 'isolate'],
            default => ['log'],
        };
        $filtered = array_values(array_unique(array_filter($actions, fn ($a) => in_array($a, $allowed, true))));

        return $filtered ?: ['log'];
    }

    public function forPc(Pc $pc): array
    {
        ['profile' => $profile, 'session' => $session, 'override' => $override] = $this->effective($pc);
        $mode = in_array($profile->mode, ['always_on', 'practice', 'test'], true) ? $profile->mode : 'always_on';
        $aiPolicy = self::resolveAiPolicy($profile, $session, $override);
        $aiPermitted = in_array($aiPolicy, ['permitted_with_disclosure', 'open'], true);
        $agent = $profile->agent ?? [];
        $net = $profile->network ?? [];
        $blockCats = $mode === 'always_on' ? [] : array_values($net['block_categories'] ?? []);
        if ($aiPermitted) {
            $blockCats = array_values(array_diff($blockCats, ['ai', 'ide']));
        }
        $blocksAi = ! $aiPermitted && ! empty($blockCats);

        $ruleActions = [];
        $disabled = [];
        foreach (Rule::query()->get(['rule_id', 'kind', 'enabled', 'actions']) as $r) {
            if (! $r->enabled) {
                $disabled[] = $r->rule_id;
            }
            if (! empty($r->actions[$mode])) {
                $isInv = in_array($r->kind, ['extension', 'artifact'], true)
                    || str_starts_with($r->rule_id, 'ext.')
                    || str_starts_with($r->rule_id, 'artifact.');
                $acts = self::clampActionsForMode($mode, array_values($r->actions[$mode]), $blocksAi, $isInv);
                if ($aiPermitted) {
                    $acts = array_values(array_diff($acts, ['kill', 'warn', 'isolate'])) ?: ['log'];
                }
                $ruleActions[$r->rule_id] = $acts;
            }
        }
        foreach ($agent['rule_actions'] ?? [] as $rid => $acts) {
            if (is_array($acts)) {
                $isInv = str_starts_with((string) $rid, 'ext.') || str_starts_with((string) $rid, 'artifact.');
                $clamped = self::clampActionsForMode($mode, array_values($acts), $blocksAi, $isInv);
                if ($aiPermitted) {
                    $clamped = array_values(array_diff($clamped, ['kill', 'warn', 'isolate'])) ?: ['log'];
                }
                $ruleActions[$rid] = $clamped;
            }
        }

        $defaultActions = match ($mode) {
            'always_on' => [
                'info' => ['log'],
                'low' => ['log'],
                'medium' => ['log'],
                'high' => ['log'],
                'critical' => ['log'],
            ],
            'practice' => [
                'info' => ['log'],
                'low' => ['log'],
                'medium' => ['log'],
                'high' => $blocksAi ? ['log', 'warn', 'alert'] : ['log', 'alert'],
                'critical' => $blocksAi ? ['log', 'warn', 'alert'] : ['log', 'alert'],
            ],
            default => [
                'info' => ['log'],
                'low' => ['log'],
                'medium' => $aiPermitted ? ['log'] : ['log', 'alert'],
                'high' => $aiPermitted ? ['log'] : ['log', 'warn', 'alert'],
                'critical' => $aiPermitted ? ['log'] : ['log', 'warn', 'alert'],
            ],
        };
        foreach ($agent['default_actions'] ?? [] as $sev => $acts) {
            if (is_array($acts) && isset($defaultActions[$sev])) {
                $clamped = self::clampActionsForMode($mode, array_values($acts), $blocksAi, false);
                if ($aiPermitted) {
                    $clamped = array_values(array_diff($clamped, ['kill', 'warn', 'isolate'])) ?: ['log'];
                }
                $defaultActions[$sev] = $clamped;
            }
        }

        $requestedTitle = $agent['title_capture'] ?? Setting::get(
            "privacy.title_capture.$mode",
            $mode === 'always_on' ? 'off' : 'app_only'
        );
        $titleCapture = self::clampTitleCapture($mode, is_string($requestedTitle) ? $requestedTitle : null);
        $collect = self::clampCollect($mode, is_array($agent['collect'] ?? null) ? $agent['collect'] : []);

        if ($mode !== 'always_on' && ! $aiPermitted && in_array('ai', $blockCats, true) && ! in_array('ide', $blockCats, true)) {
            $blockCats[] = 'ide';
        }
        $blockedDomains = [];
        if (! empty($blockCats)) {
            $blockedDomains = BlocklistEntry::query()
                ->where('enabled', true)
                ->where('kind', 'domain')
                ->whereIn('category', $blockCats)
                ->pluck('value')
                ->map(fn ($d) => strtolower(trim((string) $d)))
                ->filter()
                ->unique()
                ->values()
                ->all();
        }

        $locale = in_array(Setting::get('locale', 'en'), ['en', 'bn'], true) ? Setting::get('locale', 'en') : 'en';
        $noticeText = $locale === 'bn'
            ? trans('xephyguard.tray_notice', ['mode' => trans("xephyguard.modes.{$mode}.badge", [], 'bn')], 'bn')
            : Setting::get('notice_text');

        $targetAgentVersion = (string) ($profile->target_agent_version ?? Setting::get('target_agent_version', '') ?? '');

        $policy = array_filter([
            'pc_id' => $pc->id,
            'mode' => $mode,
            'session_id' => $session?->id ?? 0,
            'profile' => $profile->name,
            'title_capture' => $titleCapture,
            'collect' => $collect,
            'intervals' => $agent['intervals'] ?? null,
            'actions' => array_filter([
                'default' => $defaultActions,
                'rules' => $ruleActions ?: null,
            ]),
            'disabled_rules' => $disabled,
            'work_dirs' => $agent['work_dirs'] ?? Setting::get('work_dirs', ['~/lab', '~/Documents/Lab', '%USERPROFILE%\\Documents\\Lab']),
            'kill_exempt' => $agent['kill_exempt'] ?? [],
            'signatures_version' => SignatureVersion::active()?->version ?? '',
            'notice_text' => $noticeText,
            'notice_text_bn' => trans('xephyguard.tray_notice', ['mode' => trans("xephyguard.modes.{$mode}.badge", [], 'bn')], 'bn'),
        ], fn ($v) => $v !== null && $v !== []);
        $policy['ai_policy'] = $aiPolicy;
        $policy['target_agent_version'] = $targetAgentVersion;
        $policy['blocked_domains'] = $blockedDomains;
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
