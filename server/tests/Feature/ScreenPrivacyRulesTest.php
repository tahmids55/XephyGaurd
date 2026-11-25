<?php

namespace Tests\Feature;

use App\Services\PolicyService;
use App\Services\ScreenPrivacyRules;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Symfony\Component\Yaml\Yaml;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ScreenPrivacyRulesTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_shipped_list_is_valid_and_in_every_policy(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $raw = Yaml::parseFile(base_path('../data/screen/privacy_rules.yaml'))['rules'];
        $rules = app(ScreenPrivacyRules::class)->rules();

        $this->assertCount(count($raw), $rules, 'every shipped rule must be valid');
        $this->assertLessThanOrEqual(128, count($rules));
        $policy = app(PolicyService::class)->forPc($pc);
        $this->assertSame($rules, $policy['screen']['privacy_rules']);
        $this->assertContains(['process' => 'keepassxc'], $rules);
    }

    public function test_invalid_entries_are_dropped_and_extra_keys_stripped(): void
    {
        $f = tempnam(sys_get_temp_dir(), 'xgpr');
        file_put_contents($f, <<<'Y'
rules:
  - { id: ok, process: keepassxc, description: x }
  - { id: empty }
  - { id: badre, title_regex: "(unclosed" }
  - { id: long, process: "LONG" }
  - { id: num, process: 5 }
  - { id: both, process: firefox, title_regex: "bank|incognito" }
Y);
        $y = file_get_contents($f);
        file_put_contents($f, str_replace('LONG', str_repeat('a', 257), $y));

        $rules = app(ScreenPrivacyRules::class)->rules($f);
        unlink($f);

        $this->assertSame([['process' => 'keepassxc'], ['process' => 'firefox', 'title_regex' => 'bank|incognito']], $rules);
    }

    public function test_more_than_128_rules_are_truncated(): void
    {
        $f = tempnam(sys_get_temp_dir(), 'xgpr');
        $lines = array_map(fn ($i) => "  - { process: app$i }", range(1, 130));
        file_put_contents($f, "rules:\n".implode("\n", $lines)."\n");

        $this->assertCount(128, app(ScreenPrivacyRules::class)->rules($f));
        unlink($f);
    }

    public function test_missing_or_broken_file_sends_no_screen_object(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $dir = sys_get_temp_dir().'/xg-data-'.uniqid();
        mkdir("$dir/screen", 0700, true);
        file_put_contents("$dir/screen/privacy_rules.yaml", "rules: [unterminated\n");
        config(['xephyguard.data_dir' => $dir]);

        $this->assertArrayNotHasKey('screen', app(PolicyService::class)->forPc($pc));
        unlink("$dir/screen/privacy_rules.yaml");
        rmdir("$dir/screen");
        rmdir($dir);
        $this->assertArrayNotHasKey('screen', app(PolicyService::class)->forPc($pc));
    }
}
