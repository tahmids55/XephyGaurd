<?php

namespace Tests\Unit;

use App\Models\LabSession;
use App\Models\Profile;
use App\Models\SessionPc;
use App\Services\PolicyBuilder;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Queue;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/**
 * Machine-checked Contract C1 test (docs/ETHICS.md & Phase 0 C1):
 * Iterates all modes x profiles and fails if PolicyBuilder enables any collector or title_capture
 * level that the C1 table forbids, or if profile reductions are ignored.
 */
class EthicsContractTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private const C1_FORBIDDEN_COLLECT = [
        'always_on' => ['window', 'keys', 'keystats', 'clipboard', 'paste', 'files', 'gpu'],
        'practice' => ['files', 'gpu'],
        'test' => [],
    ];

    private const C1_INVENTORY_COLLECT = [
        'processes', 'ports', 'extensions', 'network', 'devices', 'artifacts',
        'model_files', 'vm', 'adapters', 'usb',
    ];

    private const C1_ALLOWED_TITLE_CAPTURE = [
        'always_on' => ['off'],
        'practice' => ['off', 'app_only'],
        'test' => ['off', 'app_only', 'full'],
    ];

    public function test_c1_contract_across_all_modes_and_profiles(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pb = app(PolicyBuilder::class);
        $sm = app(SessionManager::class);

        // Create adversarial profiles for every mode that attempt to enable all collectors,
        // full title capture, and aggressive kill/warn actions.
        $allOnCollect = [
            'processes' => true, 'ports' => true, 'extensions' => true, 'network' => true,
            'devices' => true, 'artifacts' => true, 'model_files' => true, 'vm' => true,
            'adapters' => true, 'usb' => true, 'window' => true, 'keys' => true,
            'keystats' => true, 'clipboard' => true, 'paste' => true, 'files' => true, 'gpu' => true,
        ];
        foreach (['always_on', 'practice', 'test'] as $mode) {
            Profile::create([
                'name' => "Adversarial max ($mode)",
                'mode' => $mode,
                'network' => ['block_categories' => ['ai'], 'strict_egress' => false, 'google_udm14' => false],
                'agent' => [
                    'title_capture' => 'full',
                    'collect' => $allOnCollect,
                    'default_actions' => [
                        'low' => ['log', 'warn', 'kill', 'alert', 'isolate'],
                        'high' => ['log', 'warn', 'kill', 'alert', 'isolate'],
                    ],
                    'rule_actions' => [
                        'proc.ollama' => ['log', 'warn', 'kill', 'alert', 'isolate'],
                        'ext.jb.misc' => ['log', 'warn', 'kill', 'alert'],
                    ],
                ],
            ]);
        }

        // 1. Verify Always-on mode (no active session) and custom always_on profile.
        LabSession::query()->delete();
        $alwaysPolicy = $pb->forPc($pc);
        $this->assertModeConformsToC1('always_on', $alwaysPolicy, 'default always_on');

        // Even if the built-in Always-on profile is mutated to request full titles and all collectors:
        Profile::query()->where('mode', 'always_on')->update([
            'agent' => ['title_capture' => 'full', 'collect' => $allOnCollect],
        ]);
        $mutatedAlwaysPolicy = $pb->forPc($pc);
        $this->assertModeConformsToC1('always_on', $mutatedAlwaysPolicy, 'mutated always_on');

        // 2. Iterate every profile in the database across practice and test sessions.
        foreach (Profile::query()->whereIn('mode', ['practice', 'test'])->get() as $profile) {
            $session = $sm->start($lab, $profile, 'ETHICS-101');
            $policy = $pb->forPc($pc);
            $this->assertModeConformsToC1($profile->mode, $policy, $profile->name);
            $sm->stop($session);
        }
    }

    public function test_profile_values_may_only_reduce_collection_and_title_capture(): void
    {
        Queue::fake();
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pb = app(PolicyBuilder::class);
        $sm = app(SessionManager::class);

        // In Test mode, a profile that turns off window, keys, clipboard, files, and sets title_capture=off
        // must have those reductions respected.
        $reducedTest = Profile::create([
            'name' => 'Reduced Test Profile',
            'mode' => 'test',
            'network' => ['block_categories' => ['ai']],
            'agent' => [
                'title_capture' => 'off',
                'collect' => [
                    'window' => false,
                    'keys' => false,
                    'clipboard' => false,
                    'files' => false,
                ],
            ],
        ]);
        $session = $sm->start($lab, $reducedTest, 'CS-REDUCED');
        $policy = $pb->forPc($pc);

        $this->assertSame('off', $policy['title_capture']);
        $this->assertFalse($policy['collect']['window']);
        $this->assertFalse($policy['collect']['keys']);
        $this->assertFalse($policy['collect']['keystats']);
        $this->assertFalse($policy['collect']['clipboard']);
        $this->assertFalse($policy['collect']['paste']);
        $this->assertFalse($policy['collect']['files']);
        $this->assertTrue($policy['collect']['processes']);
        $sm->stop($session);

        // In Practice mode, title_capture=full is clamped to app_only, while title_capture=off is honored.
        $practiceFull = Profile::create([
            'name' => 'Practice Full Attempt',
            'mode' => 'practice',
            'network' => [],
            'agent' => ['title_capture' => 'full', 'collect' => ['files' => true, 'window' => false]],
        ]);
        $session = $sm->start($lab, $practiceFull, 'CS-PRACTICE');
        $policy = $pb->forPc($pc);
        $this->assertSame('app_only', $policy['title_capture']);
        $this->assertFalse($policy['collect']['files']);
        $this->assertFalse($policy['collect']['window']);
        $this->assertTrue($policy['collect']['keys']);

        // Per-PC accommodation override to Always-on profile during a Test session clamps to always_on rules.
        $testSession = $sm->start($lab, $this->profile('Exam: strict'), 'CS-EXAM');
        $alwaysOn = Profile::create([
            'name' => 'Accommodation Always-on',
            'mode' => 'always_on',
            'network' => [],
            'agent' => ['title_capture' => 'off'],
        ]);
        SessionPc::query()->where('lab_session_id', $testSession->id)->where('pc_id', $pc->id)
            ->update(['override_profile_id' => $alwaysOn->id]);
        $overridePolicy = $pb->forPc($pc);
        $this->assertModeConformsToC1('always_on', $overridePolicy, 'accommodation override');
    }

    private function assertModeConformsToC1(string $mode, array $policy, string $label): void
    {
        $this->assertSame($mode, $policy['mode'], "Mode mismatch for {$label}");
        $this->assertContains(
            $policy['title_capture'],
            self::C1_ALLOWED_TITLE_CAPTURE[$mode],
            "C1 violation ({$label}): title_capture='{$policy['title_capture']}' is forbidden in mode '{$mode}'"
        );

        foreach (self::C1_FORBIDDEN_COLLECT[$mode] as $forbiddenKey) {
            $this->assertFalse(
                (bool) ($policy['collect'][$forbiddenKey] ?? false),
                "C1 violation ({$label}): collect.{$forbiddenKey} must be OFF in mode '{$mode}'"
            );
        }

        foreach (self::C1_INVENTORY_COLLECT as $invKey) {
            $this->assertArrayHasKey($invKey, $policy['collect'], "Missing inventory key {$invKey} in {$label}");
        }

        // Verify action clamping per C4/R5/R6:
        $defaultActions = $policy['actions']['default'] ?? [];
        $ruleActions = $policy['actions']['rules'] ?? [];
        if ($mode === 'always_on') {
            foreach ($defaultActions as $sev => $acts) {
                $this->assertSame(['log'], $acts, "C4/R5 violation ({$label}): always_on default actions for {$sev} must be ['log']");
            }
            foreach ($ruleActions as $rid => $acts) {
                $this->assertSame(['log'], $acts, "C4/R5 violation ({$label}): always_on rule actions for {$rid} must be ['log']");
            }
        }
        if ($mode !== 'test') {
            foreach ($defaultActions as $sev => $acts) {
                $this->assertNotContains('kill', $acts, "C4/R6 violation ({$label}): kill forbidden outside test ({$sev})");
                $this->assertNotContains('isolate', $acts, "C4/R6 violation ({$label}): isolate forbidden outside test ({$sev})");
            }
            foreach ($ruleActions as $rid => $acts) {
                $this->assertNotContains('kill', $acts, "C4/R6 violation ({$label}): kill forbidden outside test ({$rid})");
                $this->assertNotContains('isolate', $acts, "C4/R6 violation ({$label}): isolate forbidden outside test ({$rid})");
            }
        }
        foreach ($ruleActions as $rid => $acts) {
            if (str_starts_with($rid, 'ext.') || str_starts_with($rid, 'artifact.')) {
                $this->assertNotContains('warn', $acts, "R5 violation ({$label}): inventory rule {$rid} must not warn");
                $this->assertNotContains('kill', $acts, "R6 violation ({$label}): inventory rule {$rid} must not kill");
            }
        }
    }
}
