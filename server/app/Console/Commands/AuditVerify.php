<?php

declare(strict_types=1);

namespace App\Console\Commands;

use App\Services\AuditChain;
use Illuminate\Console\Command;
use Illuminate\Support\Facades\DB;

/** Walks audit_log by id and reports every row whose prev_hash does not match the row before it. */
class AuditVerify extends Command
{
    protected $signature = 'xg:audit-verify';

    protected $description = 'Check the audit log hash chain; prints the head hash to record elsewhere';

    public function handle(): int
    {
        $expected = AuditChain::GENESIS;
        $count = 0;
        $broken = [];

        DB::table('audit_log')->orderBy('id')->chunk(1000, function ($rows) use (&$expected, &$count, &$broken) {
            foreach ($rows as $row) {
                $count++;
                if ($row->prev_hash === null || ! hash_equals($expected, (string) $row->prev_hash)) {
                    $broken[] = (int) $row->id;
                }
                $expected = AuditChain::hash($row);
            }
        });

        foreach ($broken as $id) {
            $this->error("chain broken at id {$id}: the row before it was changed or removed");
        }
        if ($broken) {
            return self::FAILURE;
        }

        $this->info("audit chain ok: {$count} rows, head {$expected}");

        return self::SUCCESS;
    }
}
