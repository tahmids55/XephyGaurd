<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\LabSession;
use App\Models\Profile;
use App\Models\ResolvedPolicy;
use App\Models\Rule;
use App\Models\Setting;
use App\Services\PolicyService;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class PolicyCacheTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function queries(callable $fn): int
    {
        $n = 0;
        DB::listen(function () use (&$n) {
            $n++;
        });
        $fn();
        DB::getEventDispatcher()?->forget('Illuminate\Database\Events\QueryExecuted');

        return $n;
    }

    public function test_cached_policy_costs_a_fixed_number_of_queries(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $svc = app(PolicyService::class);
        $first = $svc->forPc($pc);
        $this->assertSame($first, $svc->forPc($pc));
        $this->assertLessThanOrEqual(2, $this->queries(fn () => $svc->forPc($pc)));
        $this->assertGreaterThan(2, $this->queries(fn () => $svc->resolve($pc)));   // the budget is meaningful
        $this->assertSame($first['version'], ResolvedPolicy::where('pc_id', $pc->id)->value('version'));
        $this->assertSame($svc->resolve($pc), $svc->forPc($pc));
    }

    public function test_every_policy_input_invalidates_the_cache(): void
    {
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $svc = app(PolicyService::class);
        $changes = [
            'setting' => fn () => Setting::put('notice_text', 'New notice '.uniqid()),
            'profile' => fn () => PolicyService::alwaysOnProfile()->update(['description' => uniqid()]),
            'rule' => fn () => Rule::query()->firstOrFail()->update(['enabled' => ! Rule::query()->firstOrFail()->enabled]),
            'session' => fn () => app(SessionManager::class)->start($lab, $this->profile('Practice: AI blocked'), 'C1'),
            'override' => fn () => app(SessionManager::class)->setOverride(
                LabSession::activeFor($lab->id), $pc, $this->profile('Test: AI blocked'), 'accommodation'),
            'switch' => fn () => app(SessionManager::class)->switchProfile(
                LabSession::activeFor($lab->id), $this->profile('Practice: AI allowed, logged')),
            'stop' => fn () => app(SessionManager::class)->stop(LabSession::activeFor($lab->id)),
        ];
        foreach ($changes as $what => $change) {
            $svc->forPc($pc);
            $gen = ResolvedPolicy::generation();
            $change();
            $this->assertGreaterThan($gen, ResolvedPolicy::generation(), $what);
            $this->assertSame($svc->resolve($pc), $svc->forPc($pc), $what);
        }
        $this->assertSame('always_on', $svc->forPc($pc)['mode']);
        $this->assertInstanceOf(Profile::class, PolicyService::alwaysOnProfile());
    }

    public function test_config_change_alone_invalidates_via_the_stamp(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $svc = app(PolicyService::class);
        $svc->forPc($pc);
        config(['xephyguard.network_mode' => 'proxy_only', 'xephyguard.ccc_ip' => '192.168.10.2']);
        $this->assertSame('proxy_only', $svc->forPc($pc)['proxy_mode']);
    }
}
