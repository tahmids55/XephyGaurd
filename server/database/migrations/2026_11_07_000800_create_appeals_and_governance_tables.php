<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::table('students', function (Blueprint $t) {
            $t->string('portal_token', 64)->nullable()->unique()->after('pseudonym');
            $t->boolean('legal_hold')->default(false)->after('exempt_reason');
        });

        Schema::table('lab_sessions', function (Blueprint $t) {
            $t->boolean('legal_hold')->default(false)->after('status');
            $t->string('ai_policy', 32)->default('forbidden')->after('legal_hold');
        });

        Schema::table('profiles', function (Blueprint $t) {
            $t->string('ai_policy', 32)->default('forbidden')->after('mode');
            $t->string('target_agent_version', 32)->nullable()->after('ai_policy');
        });

        Schema::create('appeals', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_session_id')->constrained('lab_sessions')->cascadeOnDelete();
            $t->foreignId('student_id')->constrained('students')->cascadeOnDelete();
            $t->foreignId('score_id')->nullable()->constrained('scores')->nullOnDelete();
            $t->foreignId('alert_id')->nullable()->constrained('alerts')->nullOnDelete();
            $t->string('status', 16)->default('open'); // open | upheld | overturned | noted
            $t->text('student_statement');
            $t->json('evidence_snapshot')->nullable();
            $t->text('decision_reason')->nullable();
            $t->foreignId('decided_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamp('decided_at')->nullable();
            $t->timestamps();
            $t->index(['lab_session_id', 'status']);
            $t->index(['student_id', 'status']);
        });

        Schema::create('two_person_requests', function (Blueprint $t) {
            $t->id();
            $t->string('action', 64);
            $t->string('target_type', 64)->nullable();
            $t->unsignedBigInteger('target_id')->nullable();
            $t->json('payload');
            $t->string('reason')->nullable();
            $t->foreignId('requested_by')->constrained('users')->cascadeOnDelete();
            $t->foreignId('approved_by')->nullable()->constrained('users')->nullOnDelete();
            $t->string('status', 16)->default('pending'); // pending | approved | rejected
            $t->timestamp('decided_at')->nullable();
            $t->timestamps();
            $t->index(['status', 'action']);
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('two_person_requests');
        Schema::dropIfExists('appeals');

        Schema::table('profiles', function (Blueprint $t) {
            $t->dropColumn(['ai_policy', 'target_agent_version']);
        });

        Schema::table('lab_sessions', function (Blueprint $t) {
            $t->dropColumn(['legal_hold', 'ai_policy']);
        });

        Schema::table('students', function (Blueprint $t) {
            $t->dropColumn(['portal_token', 'legal_hold']);
        });
    }
};
