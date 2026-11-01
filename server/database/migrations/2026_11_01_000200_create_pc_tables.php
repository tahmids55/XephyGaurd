<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('pcs', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->foreignId('seat_id')->nullable()->constrained()->nullOnDelete();
            $t->string('name', 64);
            $t->string('hostname', 128);
            $t->string('os', 32)->nullable();
            $t->string('os_version', 128)->nullable();
            $t->json('macs')->nullable();
            $t->string('ip', 45)->nullable();
            $t->string('status', 16)->default('offline');      // clean | warning | alert | offline
            $t->string('mode', 16)->nullable();                // last reported by the agent
            $t->string('current_user', 64)->nullable();
            $t->string('active_app', 64)->nullable();
            $t->string('last_event', 160)->nullable();
            $t->unsignedTinyInteger('live_score')->default(0);
            $t->boolean('isolated')->default(false);
            $t->timestamp('last_seen_at')->nullable();
            $t->timestamp('silent_alerted_at')->nullable();
            $t->boolean('clean_shutdown')->default(false);
            $t->json('capabilities')->nullable();
            $t->json('usage')->nullable();
            $t->timestamp('retired_at')->nullable();
            $t->timestamps();
            $t->index(['lab_id', 'retired_at']);
        });

        Schema::create('enrollments', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->string('hostname', 128);
            $t->string('os', 32)->nullable();
            $t->string('os_version', 128)->nullable();
            $t->json('macs')->nullable();
            $t->string('ip', 45)->nullable();
            $t->string('agent_version', 32)->nullable();
            $t->text('csr_pem');
            $t->string('poll_token_hash', 64);
            $t->string('status', 16)->default('pending');      // pending | approved | rejected
            $t->foreignId('pc_id')->nullable()->constrained()->nullOnDelete();
            $t->text('cert_pem')->nullable();
            $t->text('issued_token')->nullable();              // encrypted, cleared after the agent fetched it
            $t->foreignId('decided_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamp('decided_at')->nullable();
            $t->timestamps();
            $t->index('status');
        });

        Schema::create('agents', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->string('token_hash', 64)->unique();
            $t->string('cert_serial', 64)->nullable();
            $t->string('cert_fingerprint', 64)->nullable()->index();
            $t->timestamp('cert_expires_at')->nullable();
            $t->string('version', 32)->nullable();
            $t->unsignedBigInteger('last_seq')->default(0);
            $t->bigInteger('clock_offset_ms')->default(0);
            $t->string('policy_version', 16)->nullable();
            $t->string('signatures_version', 64)->nullable();
            $t->timestamp('revoked_at')->nullable();
            $t->timestamps();
        });

        Schema::create('commands', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('user_id')->nullable()->constrained()->nullOnDelete();
            $t->string('type', 32);
            $t->json('args')->nullable();
            $t->string('status', 16)->default('sent');        // sent | ok | failed | ignored
            $t->json('result')->nullable();
            $t->timestamp('acked_at')->nullable();
            $t->timestamps();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('commands');
        Schema::dropIfExists('agents');
        Schema::dropIfExists('enrollments');
        Schema::dropIfExists('pcs');
    }
};
