<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('pcs', function (Blueprint $t) {
            $t->string('machine_uid', 32)->nullable()->after('hostname');
            $t->index(['lab_id', 'machine_uid']);
        });
        Schema::table('enrollments', function (Blueprint $t) {
            $t->string('machine_uid', 32)->nullable()->after('hostname');
        });
    }

    public function down(): void
    {
        Schema::table('pcs', function (Blueprint $t) {
            $t->dropIndex(['lab_id', 'machine_uid']);
            $t->dropColumn('machine_uid');
        });
        Schema::table('enrollments', function (Blueprint $t) {
            $t->dropColumn('machine_uid');
        });
    }
};
