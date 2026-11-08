<?php

namespace App\Services;

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
            ?? new Profile(['name' => 'Always-on', 'mode' => 'always_on', 'network' => [], 'agent' => []]);
    }

    public function forPc(Pc $pc): array
    {
        ['profile' => $profile, 'session' => $session] = $this->effective($pc);
        $mode = $profile->mode;
        $agent = $profile->agent ?? [];

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
            'title_capture' => Setting::get("privacy.title_capture.$mode", 'full'),
            'collect' => array_merge(['window' => true], $agent['collect'] ?? []),
            'intervals' => $agent['intervals'] ?? null,
            'actions' => array_filter(['default' => $agent['default_actions'] ?? null, 'rules' => $ruleActions ?: null]),
            'disabled_rules' => $disabled,
            'work_dirs' => $agent['work_dirs'] ?? Setting::get('work_dirs', ['~/lab', '~/Documents/Lab', '%USERPROFILE%\\Documents\\Lab']),
            'kill_exempt' => $agent['kill_exempt'] ?? [],
            'signatures_version' => SignatureVersion::active()?->version ?? '',
            'notice_text' => Setting::get('notice_text'),
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
