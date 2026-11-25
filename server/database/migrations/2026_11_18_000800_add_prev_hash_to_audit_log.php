<?php

declare(strict_types=1);

use App\Services\AuditChain;
use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('audit_log', fn (Blueprint $t) => $t->char('prev_hash', 64)->nullable()->after('id'));

        // Chain the rows written before this step, oldest first (DECISIONS #29).
        $prev = AuditChain::GENESIS;
        DB::table('audit_log')->orderBy('id')->chunk(500, function ($rows) use (&$prev) {
            foreach ($rows as $row) {
                DB::table('audit_log')->where('id', $row->id)->update(['prev_hash' => $prev]);
                $row->prev_hash = $prev;
                $prev = AuditChain::hash($row);
            }
        });
    }

    public function down(): void
    {
        Schema::table('audit_log', fn (Blueprint $t) => $t->dropColumn('prev_hash'));
    }
};
