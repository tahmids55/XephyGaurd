<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/** Per-session network lock for proxy-only mode (DECISIONS #23): off | observe | strict. */
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('lab_sessions', function (Blueprint $t) {
            $t->string('network_lock', 8)->default('observe')->after('mode');
        });
    }

    public function down(): void
    {
        Schema::table('lab_sessions', function (Blueprint $t) {
            $t->dropColumn('network_lock');
        });
    }
};
