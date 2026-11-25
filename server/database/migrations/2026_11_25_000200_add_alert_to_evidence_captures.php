<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/** F123: an auto screenshot names its alert and the `evidence_snapshot` command whose nonce it used (single use). */
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('evidence_captures', function (Blueprint $t) {
            $t->foreignId('alert_id')->nullable()->after('screen_view_audit_id')->constrained()->cascadeOnDelete();
            $t->unsignedBigInteger('command_id')->nullable()->unique()->after('alert_id');
        });
    }

    public function down(): void
    {
        Schema::table('evidence_captures', function (Blueprint $t) {
            $t->dropConstrainedForeignId('alert_id');
            $t->dropUnique(['command_id']);
            $t->dropColumn('command_id');
        });
    }
};
