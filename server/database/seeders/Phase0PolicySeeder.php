<?php

namespace Database\Seeders;

use App\Models\BlocklistEntry;
use App\Models\Profile;
use App\Models\Rule;
use App\Services\Bootstrap;
use Illuminate\Database\Seeder;

/**
 * Reproducible, idempotent seeder capturing the IDE blocklist domains and process/port rule
 * refinements previously applied via direct SQLite edits, while restoring built-in profile
 * title_capture settings per Contract C1 (R3).
 */
class Phase0PolicySeeder extends Seeder
{
    public const IDE_BLOCKLIST_DOMAINS = [
        'api.individual.githubcopilot.com',
        'api.business.githubcopilot.com',
        'api.enterprise.githubcopilot.com',
        'default.exp-tas.com',
    ];

    public const CHATGPT_DESKTOP_EXES = [
        'chatgpt',
        'claude desktop',
        'copilot',
        'microsoft copilot',
        'copilotapphost',
        'github copilot',
        'perplexity',
    ];

    public const PROBE_REQUIRED_PORT_RULES = [
        'port.koboldcpp',
        'port.textgen',
        'port.vllm',
        'port.llamacpp',
    ];

    public function run(): void
    {
        self::sync();
    }

    public static function sync(): void
    {
        foreach (self::IDE_BLOCKLIST_DOMAINS as $domain) {
            BlocklistEntry::query()->firstOrCreate(
                ['category' => 'ide', 'value' => $domain],
                ['kind' => 'domain', 'source' => 'seed', 'enabled' => true]
            );
        }

        if ($desktopRule = Rule::query()->where('rule_id', 'proc.desktop.chatgpt')->first()) {
            $def = is_array($desktopRule->definition) ? $desktopRule->definition : [];
            $existingExes = $def['match']['exe'] ?? [];
            $mergedExes = array_values(array_unique(array_merge($existingExes, self::CHATGPT_DESKTOP_EXES)));
            $def['match']['exe'] = $mergedExes;
            $desktopRule->update(['definition' => $def]);
        }

        foreach (self::PROBE_REQUIRED_PORT_RULES as $ruleId) {
            if ($portRule = Rule::query()->where('rule_id', $ruleId)->first()) {
                $def = is_array($portRule->definition) ? $portRule->definition : [];
                if (($def['requires_probe'] ?? null) !== true) {
                    $def['requires_probe'] = true;
                    $portRule->update(['definition' => $def]);
                }
            }
        }

        // Restore built-in profile title_capture defaults per Contract C1 (R3).
        foreach (Bootstrap::PROFILES as $p) {
            if ($profile = Profile::query()->where('name', $p['name'])->where('builtin', true)->first()) {
                $agent = is_array($profile->agent) ? $profile->agent : [];
                $expectedTitle = $p['agent']['title_capture'] ?? ($p['mode'] === 'always_on' ? 'off' : 'app_only');
                if (($agent['title_capture'] ?? null) !== $expectedTitle) {
                    $agent['title_capture'] = $expectedTitle;
                    $profile->update(['agent' => $agent]);
                }
            }
        }
    }
}
