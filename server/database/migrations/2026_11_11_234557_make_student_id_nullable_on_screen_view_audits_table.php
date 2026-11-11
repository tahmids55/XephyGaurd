<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('screen_view_audits', function (Blueprint $table) {
            $table->foreignId('student_id')->nullable()->change();
            $table->foreignId('pc_id')->nullable()->after('student_id')->constrained('pcs')->nullOnDelete();
        });
    }

    public function down(): void
    {
        Schema::table('screen_view_audits', function (Blueprint $table) {
            $table->dropForeign(['pc_id']);
            $table->dropColumn('pc_id');
        });
    }
};
