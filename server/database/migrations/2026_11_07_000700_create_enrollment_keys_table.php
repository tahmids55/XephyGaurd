<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('enrollment_keys', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->string('label', 120);
            $t->string('key_prefix', 24)->index();
            $t->string('key_hash', 64)->unique();
            $t->timestamp('expires_at')->nullable();
            $t->unsignedInteger('max_uses')->nullable();
            $t->unsignedInteger('uses_count')->default(0);
            $t->json('allowed_macs')->nullable();
            $t->timestamp('revoked_at')->nullable();
            $t->foreignId('created_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamps();
            $t->index(['lab_id', 'revoked_at']);
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('enrollment_keys');
    }
};
