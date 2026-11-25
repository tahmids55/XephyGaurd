<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-12: evidence is accepted only inside an audited live view; remember which one.
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('evidence_captures', function (Blueprint $table) {
            $table->foreignId('screen_view_audit_id')->nullable()->after('captured_by')
                ->constrained('screen_view_audits')->nullOnDelete();
        });
    }

    public function down(): void
    {
        Schema::table('evidence_captures', function (Blueprint $table) {
            $table->dropConstrainedForeignId('screen_view_audit_id');
        });
    }
};
