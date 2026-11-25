<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use Tests\TestCase;

/** S-32 (I-SERVER-41): the open-alert count and lists use an index on alerts.acknowledged_at. */
class AlertIndexTest extends TestCase
{
    use RefreshDatabase;

    public function test_acknowledged_at_is_indexed(): void
    {
        $this->assertTrue(Schema::hasIndex('alerts', ['acknowledged_at']));
    }

    public function test_open_alert_count_plan_uses_the_index(): void
    {
        $sql = Alert::query()->whereNull('acknowledged_at')->toRawSql();
        $plan = collect(DB::select('EXPLAIN QUERY PLAN '.$sql))->pluck('detail')->implode(' | ');

        $this->assertStringContainsString('alerts_acknowledged_at_index', $plan, $plan);
        $this->assertStringNotContainsString('SCAN alerts', $plan, $plan);
    }
}
