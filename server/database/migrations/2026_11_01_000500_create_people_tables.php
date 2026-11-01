<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('students', function (Blueprint $t) {
            $t->id();
            $t->string('student_number', 32)->unique();
            $t->string('name', 128);
            $t->string('login', 64)->index();                    // OS login used on lab PCs
            $t->string('pseudonym', 16)->unique();               // stable research identifier
            $t->boolean('exempt')->default(false);               // accommodation: never flagged
            $t->string('exempt_reason')->nullable();
            $t->timestamps();
        });

        Schema::create('student_pc_assignments', function (Blueprint $t) {
            $t->id();
            $t->foreignId('student_id')->constrained()->cascadeOnDelete();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('lab_session_id')->nullable()->constrained()->nullOnDelete();
            $t->timestamp('first_seen_at');
            $t->timestamp('last_seen_at');
            $t->index(['lab_session_id', 'student_id']);
        });

        Schema::create('consents', function (Blueprint $t) {
            $t->id();
            $t->foreignId('student_id')->constrained()->cascadeOnDelete();
            $t->string('purpose', 32)->default('research');
            $t->boolean('granted');
            $t->string('form_version', 16)->default('1.0');
            $t->foreignId('recorded_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamp('recorded_at');
            $t->timestamps();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('consents');
        Schema::dropIfExists('student_pc_assignments');
        Schema::dropIfExists('students');
    }
};
