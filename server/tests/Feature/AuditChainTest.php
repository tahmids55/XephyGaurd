<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Services\Audit;
use App\Services\AuditChain;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Tests\TestCase;

class AuditChainTest extends TestCase
{
    use RefreshDatabase;

    /** @return list<int> */
    private function logThree(): array
    {
        return [
            Audit::log('a.one', null, ['b' => 2, 'a' => 1])->id,
            Audit::log('a.two')->id,
            Audit::log('a.three', null, ['x' => 'y'])->id,
        ];
    }

    public function test_rows_chain_and_verify_passes(): void
    {
        [$one, $two, $three] = $this->logThree();
        $rows = DB::table('audit_log')->orderBy('id')->get();

        $this->assertSame(AuditChain::GENESIS, $rows[0]->prev_hash);
        $this->assertSame(AuditChain::hash($rows[0]), $rows[1]->prev_hash);
        $this->assertSame(AuditChain::hash($rows[1]), $rows[2]->prev_hash);

        $this->artisan('xg:audit-verify')
            ->expectsOutput('audit chain ok: 3 rows, head '.AuditChain::hash($rows[2]))
            ->assertSuccessful();
    }

    public function test_edited_row_breaks_the_chain_at_the_next_row(): void
    {
        [$one, $two, $three] = $this->logThree();
        DB::table('audit_log')->where('id', $one)->update(['details' => json_encode(['a' => 9])]);

        $this->artisan('xg:audit-verify')
            ->expectsOutput("chain broken at id {$two}: the row before it was changed or removed")
            ->assertFailed();
    }

    public function test_deleted_row_breaks_the_chain(): void
    {
        [$one, $two, $three] = $this->logThree();
        DB::table('audit_log')->where('id', $two)->delete();

        $this->artisan('xg:audit-verify')
            ->expectsOutput("chain broken at id {$three}: the row before it was changed or removed")
            ->assertFailed();
    }

    public function test_row_written_outside_audit_log_is_reported(): void
    {
        $this->logThree();
        $rogue = AuditLog::create(['action' => 'forged']);

        $this->artisan('xg:audit-verify')
            ->expectsOutput("chain broken at id {$rogue->id}: the row before it was changed or removed")
            ->assertFailed();
    }

    public function test_hash_ignores_json_key_order(): void
    {
        $row = (object) ['id' => 1, 'prev_hash' => AuditChain::GENESIS, 'user_id' => null, 'action' => 'x',
            'subject_type' => null, 'subject_id' => '5', 'details' => '{"b":2,"a":1}', 'ip' => null,
            'created_at' => '2026-11-18 10:00:00'];
        $same = clone $row;
        $same->details = '{"a": 1, "b": 2}';
        $same->subject_id = 5;

        $this->assertSame(AuditChain::hash($row), AuditChain::hash($same));
    }
}
