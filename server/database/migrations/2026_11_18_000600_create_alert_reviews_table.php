<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-31 (DECISIONS #29): append-only record of who reviewed an alert, when, and the note.
return new class extends Migration
{
    public function up(): void
    {
        Schema::create('alert_reviews', function (Blueprint $t) {
            $t->id();
            $t->foreignId('alert_id')->constrained('alerts')->cascadeOnDelete();
            $t->foreignId('user_id')->nullable()->constrained('users')->nullOnDelete();
            $t->text('note')->nullable();
            $t->timestamp('reviewed_at');
            $t->index('alert_id');
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('alert_reviews');
    }
};
