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
            ?? new Profile(['name' => 'Always-on', 'mode' => 'always_on', 'network' => [], 'agent' => ['title_capture' => 'off']]);
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
        ['profile' => $profile, 'session' => $session] = $this->effective($pc);
        $mode = in_array($profile->mode, ['always_on', 'practice', 'test'], true) ? $profile->mode : 'always_on';
        $agent = $profile->agent ?? [];
        $net = $profile->network ?? [];
        $blockCats = $mode === 'always_on' ? [] : array_values($net['block_categories'] ?? []);
        $blocksAi = ! empty($blockCats);

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
                $ruleActions[$r->rule_id] = self::clampActionsForMode($mode, array_values($r->actions[$mode]), $blocksAi, $isInv);
            }
        }
        foreach ($agent['rule_actions'] ?? [] as $rid => $acts) {
            if (is_array($acts)) {
                $isInv = str_starts_with((string) $rid, 'ext.') || str_starts_with((string) $rid, 'artifact.');
                $ruleActions[$rid] = self::clampActionsForMode($mode, array_values($acts), $blocksAi, $isInv);
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
                'medium' => ['log', 'alert'],
                'high' => ['log', 'warn', 'alert'],
                'critical' => ['log', 'warn', 'alert'],
            ],
        };
        foreach ($agent['default_actions'] ?? [] as $sev => $acts) {
            if (is_array($acts) && isset($defaultActions[$sev])) {
                $defaultActions[$sev] = self::clampActionsForMode($mode, array_values($acts), $blocksAi, false);
            }
        }

        $requestedTitle = $agent['title_capture'] ?? Setting::get(
            "privacy.title_capture.$mode",
            $mode === 'always_on' ? 'off' : 'app_only'
        );
        $titleCapture = self::clampTitleCapture($mode, is_string($requestedTitle) ? $requestedTitle : null);
        $collect = self::clampCollect($mode, is_array($agent['collect'] ?? null) ? $agent['collect'] : []);

        if ($mode !== 'always_on' && in_array('ai', $blockCats, true) && ! in_array('ide', $blockCats, true)) {
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
            'notice_text' => Setting::get('notice_text'),
        ], fn ($v) => $v !== null && $v !== []);
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
