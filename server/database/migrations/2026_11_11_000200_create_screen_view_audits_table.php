<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('screen_view_audits', function (Blueprint $table) {
            $table->id();
            $table->foreignId('instructor_id')->constrained('users')->cascadeOnDelete();
            $table->foreignId('student_id')->constrained('students')->cascadeOnDelete();
            $table->foreignId('lab_session_id')->nullable()->constrained('lab_sessions')->nullOnDelete();
            $table->unsignedBigInteger('stream_id')->nullable();
            $table->string('mode', 32)->default('live');
            $table->timestamp('started_at');
            $table->timestamp('ended_at')->nullable();
            $table->timestamps();

            $table->index(['instructor_id', 'started_at']);
            $table->index(['student_id', 'started_at']);
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('screen_view_audits');
    }
};
