<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-14 (DECISIONS #28): one-time per-PC enrollment tokens, stored hashed.
return new class extends Migration
{
    public function up(): void
    {
        Schema::create('enrollment_tokens', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained('labs')->cascadeOnDelete();
            $t->string('token_hash', 64)->unique();
            $t->string('pc_name', 64)->nullable();
            $t->foreignId('created_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamp('expires_at');
            $t->timestamp('used_at')->nullable();
            $t->foreignId('enrollment_id')->nullable()->constrained('enrollments')->nullOnDelete();
            $t->timestamp('revoked_at')->nullable();
            $t->timestamps();
            $t->index(['lab_id', 'used_at']);
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('enrollment_tokens');
    }
};
