<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Lab;
use App\Models\Profile;
use App\Models\Setting;

/** Idempotent first-install data: lab row, built-in profiles, seed blocklists and rules. */
class Bootstrap
{
    public function __construct(private readonly BlocklistService $blocklists, private readonly SignatureCompiler $signatures) {}

    /**
     * ETHICS.md "What is collected": what each mode never collects. Built-in profiles are created this way
     * and clamped to it on every bootstrap (only ever switched off, never on). Titles: Always-on off.
     */
    public const ETHICS_OFF = ['always_on' => ['window', 'clipboard', 'keys', 'files'], 'practice' => ['files'], 'test' => []];

    private const COLLECT_ALWAYS = ['window' => false, 'clipboard' => false, 'keys' => false, 'files' => false, 'artifacts' => true];

    private const COLLECT_PRACTICE = ['window' => true, 'clipboard' => true, 'keys' => true, 'files' => false, 'artifacts' => true];

    private const COLLECT_TEST = ['window' => true, 'clipboard' => true, 'keys' => true, 'files' => true, 'artifacts' => true];

    public const PROFILES = [
        ['name' => 'Always-on', 'mode' => 'always_on', 'description' => 'No session: heartbeat, process/extension scan, adapter changes, browser tab monitor. No window, typing, clipboard or file data. Logging only, no blocking.',
            'network' => ['block_categories' => [], 'strict_egress' => false, 'google_udm14' => false],
            'agent' => ['title_capture' => 'off', 'collect' => self::COLLECT_ALWAYS]],
        ['name' => 'Practice: AI allowed, logged', 'mode' => 'practice', 'description' => 'Normal lab class. AI sites reachable; light behavior metrics; tampering and local-LLM alerts.',
            'network' => ['block_categories' => [], 'strict_egress' => false, 'google_udm14' => false],
            'agent' => ['title_capture' => 'app_only', 'collect' => self::COLLECT_PRACTICE]],
        ['name' => 'Practice: AI blocked', 'mode' => 'practice', 'description' => 'Normal lab class with AI chat sites blocked (DNS + SNI), other egress open.',
            'network' => ['block_categories' => ['ai'], 'strict_egress' => false, 'google_udm14' => true],
            'agent' => ['title_capture' => 'app_only', 'collect' => self::COLLECT_PRACTICE]],
        ['name' => 'Test: AI blocked', 'mode' => 'test', 'description' => 'Lab test. Full AI blocking incl. IDE backends and marketplaces; strict egress; all rules active.',
            'network' => ['block_categories' => ['ai', 'ide', 'marketplace', 'vpn', 'custom'], 'strict_egress' => true, 'google_udm14' => true],
            'agent' => ['title_capture' => 'app_only', 'collect' => self::COLLECT_TEST]],
        ['name' => 'Exam: strict', 'mode' => 'test', 'description' => 'Exam. As Test, plus full window titles (if the Test title setting allows them) and killing of local LLM runtimes.',
            'network' => ['block_categories' => ['ai', 'ide', 'marketplace', 'vpn', 'custom'], 'strict_egress' => true, 'google_udm14' => true],
            'agent' => ['title_capture' => 'full', 'collect' => self::COLLECT_TEST, 'rule_actions' => [
                'proc.ollama' => ['log', 'warn', 'kill', 'alert'], 'proc.llamacpp' => ['log', 'warn', 'kill', 'alert'],
                'proc.lmstudio' => ['log', 'warn', 'kill', 'alert'], 'proc.koboldcpp' => ['log', 'warn', 'kill', 'alert']]]],
    ];

    public function run(): array
    {
        $out = [];
        $lab = Lab::query()->first() ?? Lab::create(['name' => config('xephyguard.lab_name'),
            'subnet' => config('xephyguard.lab_subnet'), 'ccc_ip' => config('xephyguard.ccc_ip')]);
        if (! $lab->enroll_key_hash) {
            $lab->rotateEnrollKey();
            $out[] = 'enrollment key generated';
        }
        foreach (self::PROFILES as $p) {
            $prof = Profile::query()->firstOrCreate(['name' => $p['name']], $p + ['builtin' => true]);
            if (! $prof->wasRecentlyCreated && $prof->builtin) {
                $this->clampToEthics($prof);
            }
        }
        $out[] = 'blocklist entries added: '.$this->blocklists->importSeed();
        $out[] = 'rules added: '.$this->signatures->importSeed();
        if (CertificateAuthority::fromConfig()->exists()) {
            try {
                $v = $this->signatures->publish(null, 'bootstrap');
                $out[] = 'signatures: '.$v->version;
            } catch (\RuntimeException $e) {
                $out[] = 'signatures: NOT published: '.$e->getMessage();
            }
        } else {
            $out[] = 'signatures: not published (no CA yet; run xephyguard:ca:init)';
        }
        if (Setting::get('google_hide_selectors') === null || Setting::get('google_hide_selectors') === []) {
            // Fallback only (udm=14 is the primary mechanism). Verify against current markup: eval/live_checks.
            Setting::put('google_hide_selectors', ['div[jsname="txosbe"]', 'div[data-attrid="AIOverview"]', '#m-x-content', 'g-ai-overview']);
        }

        return $out;
    }

    /** Switches off what ETHICS.md forbids for the profile's mode; never switches anything on. */
    private function clampToEthics(Profile $prof): void
    {
        $agent = $prof->agent ?? [];
        $collect = $agent['collect'] ?? [];
        foreach (self::ETHICS_OFF[$prof->mode] ?? array_keys(self::COLLECT_TEST) as $k) {
            $collect[$k] = false;
        }
        $collect += ['artifacts' => true];                    // local-model scan is allowed in every mode
        if ($prof->mode === 'test') {
            $collect += ['files' => true];                    // older rows predate the files collector
        }
        $agent['collect'] = $collect;
        if ($prof->mode === 'always_on') {
            $agent['title_capture'] = 'off';
        }
        if ($agent !== ($prof->agent ?? [])) {
            $prof->update(['agent' => $agent]);
        }
    }
}
