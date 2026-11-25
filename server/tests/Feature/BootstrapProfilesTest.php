<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Profile;
use App\Services\Bootstrap;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class BootstrapProfilesTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    /** ETHICS.md "What is collected", per mode: window, clipboard, keys, files. */
    private const ETHICS = [
        'always_on' => ['window' => false, 'clipboard' => false, 'keys' => false, 'files' => false],
        'practice' => ['window' => true, 'clipboard' => true, 'keys' => true, 'files' => false],
        'test' => ['window' => true, 'clipboard' => true, 'keys' => true, 'files' => true],
    ];

    public function test_fresh_builtin_profiles_match_the_ethics_table(): void
    {
        $this->bootLab();
        foreach (Profile::where('builtin', true)->get() as $p) {
            $c = $p->agent['collect'];
            foreach (self::ETHICS[$p->mode] as $k => $want) {
                $this->assertSame($want, $c[$k], "{$p->name}: $k");
            }
            $this->assertTrue($c['artifacts'], $p->name);
            $title = $p->agent['title_capture'];
            $p->mode === 'always_on' ? $this->assertSame('off', $title)
                : $this->assertContains($title, $p->name === 'Exam: strict' ? ['full'] : ['app_only'], $p->name);
        }
        $this->assertSame(5, Profile::where('builtin', true)->count());
    }

    public function test_bootstrap_clamps_old_builtin_rows_and_never_switches_anything_on(): void
    {
        $this->bootLab();
        $always = Profile::where('mode', 'always_on')->where('builtin', true)->firstOrFail();
        $always->update(['agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]]);
        $practice = Profile::where('name', 'Practice: AI blocked')->firstOrFail();
        $practice->update(['agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'clipboard' => false, 'keys' => true, 'files' => true]]]);
        $test = Profile::where('name', 'Test: AI blocked')->firstOrFail();
        $test->update(['agent' => ['title_capture' => 'full', 'collect' => ['window' => true, 'keys' => false]]]);
        $custom = Profile::create(['name' => 'Mine', 'mode' => 'practice', 'builtin' => false,
            'agent' => ['collect' => ['files' => true]], 'network' => []]);

        app(Bootstrap::class)->run();

        $a = $always->fresh()->agent;
        $this->assertSame('off', $a['title_capture']);
        $this->assertSame([false, false, false, false], [$a['collect']['window'], $a['collect']['clipboard'], $a['collect']['keys'], $a['collect']['files']]);
        $pr = $practice->fresh()->agent;
        $this->assertFalse($pr['collect']['files']);
        $this->assertFalse($pr['collect']['clipboard']);       // the admin's "off" stays off
        $this->assertSame('full', $pr['title_capture']);       // titles are capped by the setting (stricter wins), not rewritten
        $t = $test->fresh()->agent['collect'];
        $this->assertFalse($t['keys']);
        $this->assertTrue($t['files']);
        $this->assertTrue($custom->fresh()->agent['collect']['files']);   // custom profiles are the admin's
    }
}
