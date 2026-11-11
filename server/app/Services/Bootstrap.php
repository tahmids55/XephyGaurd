<?php

namespace App\Services;

use App\Models\Lab;
use App\Models\Profile;
use App\Models\Setting;

/** Idempotent first-install data: lab row, built-in profiles, seed blocklists and rules. */
class Bootstrap
{
    public function __construct(private readonly BlocklistService $blocklists, private readonly SignatureCompiler $signatures) {}

    public const PROFILES = [
        ['name' => 'Always-on', 'mode' => 'always_on', 'description' => 'No session: heartbeat, process/extension scan, adapter changes, browser tab monitor. Logging only, no blocking.',
            'network' => ['block_categories' => [], 'strict_egress' => false, 'google_udm14' => false],
            'agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]],
        ['name' => 'Practice: AI allowed, logged', 'mode' => 'practice', 'description' => 'Normal lab class. AI sites reachable; light behavior metrics; tampering and local-LLM alerts.',
            'network' => ['block_categories' => [], 'strict_egress' => false, 'google_udm14' => false],
            'agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]],
        ['name' => 'Practice: AI blocked', 'mode' => 'practice', 'description' => 'Normal lab class with AI chat sites blocked (DNS + SNI), other egress open.',
            'network' => ['block_categories' => ['ai'], 'strict_egress' => false, 'google_udm14' => true],
            'agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]],
        ['name' => 'Test: AI blocked', 'mode' => 'test', 'description' => 'Lab test. Full AI blocking incl. IDE backends and marketplaces; strict egress; all rules active.',
            'network' => ['block_categories' => ['ai', 'ide', 'marketplace', 'vpn', 'custom'], 'strict_egress' => true, 'google_udm14' => true],
            'agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]],
        ['name' => 'Exam: strict', 'mode' => 'test', 'description' => 'Exam. As Test, plus full window titles and killing of local LLM runtimes.',
            'network' => ['block_categories' => ['ai', 'ide', 'marketplace', 'vpn', 'custom'], 'strict_egress' => true, 'google_udm14' => true],
            'agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true], 'rule_actions' => [
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
            Profile::query()->firstOrCreate(['name' => $p['name']], $p + ['builtin' => true]);
        }
        $out[] = 'blocklist entries added: '.$this->blocklists->importSeed();
        $out[] = 'rules added: '.$this->signatures->importSeed();
        if (CertificateAuthority::fromConfig()->exists()) {
            $v = $this->signatures->publish(null, 'bootstrap');
            $out[] = 'signatures: '.$v->version;
        } else {
            $out[] = 'signatures: not published (no CA yet; run xephyguard:ca:init)';
        }
        if (Setting::get('google_hide_selectors') === null || Setting::get('google_hide_selectors') === []) {
            // Fallback only (udm=14 is the primary mechanism). Verify against current markup: eval/live_checks.
            Setting::put('google_hide_selectors', ['div[jsname="txosbe"]', 'div[data-attrid="AIOverview"]', '#m-x-content', 'g-ai-overview']);
        }

        return $out;
    }
}
