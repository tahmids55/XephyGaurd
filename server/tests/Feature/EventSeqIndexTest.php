<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Event;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use Tests\TestCase;

/** S-47 (I-SERVER-28): snapshot lookups and deletes use an index on events (pc_id, type, seq). */
class EventSeqIndexTest extends TestCase
{
    use RefreshDatabase;

    public function test_pc_type_seq_is_indexed(): void
    {
        $this->assertTrue(Schema::hasIndex('events', ['pc_id', 'type', 'seq']));
    }

    public function test_latest_snapshot_lookup_plan_uses_the_index(): void
    {
        $sql = Event::query()->where('pc_id', 1)->where('type', 'process_snapshot')->latest('seq')->limit(1)->toRawSql();
        $this->assertPlanUsesIndex($sql);
    }

    public function test_old_snapshot_delete_plan_uses_the_index(): void
    {
        $sql = Event::query()->where('pc_id', 1)->where('type', 'process_snapshot')->where('seq', '<', 10)->toRawSql();
        $this->assertPlanUsesIndex($sql);
    }

    private function assertPlanUsesIndex(string $sql): void
    {
        $plan = collect(DB::select('EXPLAIN QUERY PLAN '.$sql))->pluck('detail')->implode(' | ');

        $this->assertStringContainsString('events_pc_id_type_seq_index', $plan, $plan);
        $this->assertStringNotContainsString('SCAN events', $plan, $plan);
    }
}
