<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-47 (I-SERVER-28): the inspector's latest-snapshot lookups and the per-batch snapshot
// deletes in EventIngestor filter on (pc_id, type) and order/compare on seq.
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('events', function (Blueprint $t) {
            $t->index(['pc_id', 'type', 'seq']);
        });
    }

    public function down(): void
    {
        Schema::table('events', function (Blueprint $t) {
            $t->dropIndex(['pc_id', 'type', 'seq']);
        });
    }
};
