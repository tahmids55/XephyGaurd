<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('users', function (Blueprint $t) {
            $t->string('role', 16)->default('assistant')->after('email');   // admin | instructor | assistant
            $t->boolean('active')->default(true)->after('role');
            $t->timestamp('last_login_at')->nullable();
        });

        Schema::create('settings', function (Blueprint $t) {
            $t->string('key', 64)->primary();
            $t->json('value')->nullable();
            $t->timestamps();
        });

        Schema::create('labs', function (Blueprint $t) {
            $t->id();
            $t->string('name');
            $t->string('subnet', 43)->nullable();
            $t->string('ccc_ip', 45)->nullable();
            $t->string('enroll_key_hash', 64)->nullable();
            $t->string('enroll_key_hint', 8)->nullable();
            $t->timestamp('enroll_key_rotated_at')->nullable();
            $t->boolean('auto_approve')->default(false);
            $t->json('auto_approve_macs')->nullable();
            $t->unsignedSmallInteger('grid_cols')->default(8);
            $t->unsignedSmallInteger('grid_rows')->default(5);
            $t->timestamps();
        });

        Schema::create('seats', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->string('label', 32);
            $t->unsignedSmallInteger('x')->default(0);
            $t->unsignedSmallInteger('y')->default(0);
            $t->timestamps();
            $t->unique(['lab_id', 'x', 'y']);
        });

        // Append-only: no update/delete paths exist in the application (see AuditLog model).
        Schema::create('audit_log', function (Blueprint $t) {
            $t->id();
            $t->foreignId('user_id')->nullable()->constrained()->nullOnDelete();
            $t->string('action', 64);
            $t->string('subject_type', 64)->nullable();
            $t->unsignedBigInteger('subject_id')->nullable();
            $t->json('details')->nullable();
            $t->string('ip', 45)->nullable();
            $t->timestamp('created_at')->useCurrent();
            $t->index(['subject_type', 'subject_id']);
            $t->index('created_at');
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('audit_log');
        Schema::dropIfExists('seats');
        Schema::dropIfExists('labs');
        Schema::dropIfExists('settings');
        Schema::table('users', fn (Blueprint $t) => $t->dropColumn(['role', 'active', 'last_login_at']));
    }
};
