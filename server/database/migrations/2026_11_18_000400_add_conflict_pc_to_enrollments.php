<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-17: a PC that only looks like the enrolling machine (hostname/MAC) is flagged for staff, never merged.
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('enrollments', function (Blueprint $t) {
            $t->foreignId('conflict_pc_id')->nullable()->after('pc_id')->constrained('pcs')->nullOnDelete();
        });
    }

    public function down(): void
    {
        Schema::table('enrollments', function (Blueprint $t) {
            $t->dropConstrainedForeignId('conflict_pc_id');
        });
    }
};
