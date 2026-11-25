<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

/** F123: context frozen when an alert is raised (DECISIONS #32). Canonical JSON text + its sha256. */
return new class extends Migration
{
    public function up(): void
    {
        Schema::create('alert_snapshots', function (Blueprint $t) {
            $t->id();
            $t->foreignId('alert_id')->unique()->constrained()->cascadeOnDelete();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->timestamp('captured_at', 3);
            $t->longText('payload');
            $t->char('sha256', 64);
            $t->index('captured_at');
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('alert_snapshots');
    }
};
