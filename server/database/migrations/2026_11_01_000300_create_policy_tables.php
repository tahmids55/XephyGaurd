<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        Schema::create('profiles', function (Blueprint $t) {
            $t->id();
            $t->string('name', 64)->unique();
            $t->string('mode', 16);                               // always_on | practice | test
            $t->text('description')->nullable();
            $t->json('network');                                  // {block_categories:[], log_urls:bool, strict_egress:bool, google_udm14:bool}
            $t->json('agent');                                    // overrides merged into the agent Policy JSON
            $t->boolean('builtin')->default(false);
            $t->timestamps();
        });

        // "sessions" is Laravel's login-session table, so lab sessions live in lab_sessions.
        Schema::create('lab_sessions', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->foreignId('profile_id')->constrained();
            $t->foreignId('timetable_entry_id')->nullable();
            $t->string('course', 64);
            $t->string('title', 128)->nullable();
            $t->string('mode', 16);
            $t->string('status', 16)->default('scheduled');      // scheduled | active | ended
            $t->timestamp('starts_at');
            $t->timestamp('ends_at')->nullable();
            $t->timestamp('started_at')->nullable();
            $t->timestamp('ended_at')->nullable();
            $t->foreignId('started_by')->nullable()->constrained('users')->nullOnDelete();
            // research
            $t->string('experiment_condition', 32)->nullable();  // baseline | treatment
            $t->string('experiment_group', 32)->nullable();
            $t->timestamps();
            $t->index(['lab_id', 'status']);
        });

        Schema::create('session_pcs', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_session_id')->constrained()->cascadeOnDelete();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('override_profile_id')->nullable()->constrained('profiles')->nullOnDelete();
            $t->string('override_reason')->nullable();
            $t->foreignId('student_id')->nullable();
            $t->timestamps();
            $t->unique(['lab_session_id', 'pc_id']);
        });

        Schema::create('timetable_entries', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_id')->constrained()->cascadeOnDelete();
            $t->foreignId('profile_id')->constrained();
            $t->string('course', 64);
            $t->unsignedTinyInteger('weekday');                  // 1 = Monday ... 7 = Sunday (ISO)
            $t->time('start_time');
            $t->time('end_time');
            $t->date('valid_from')->nullable();
            $t->date('valid_until')->nullable();
            $t->boolean('enabled')->default(true);
            $t->timestamps();
        });

        Schema::create('rules', function (Blueprint $t) {
            $t->id();
            $t->string('rule_id', 64)->unique();                 // e.g. proc.ollama
            $t->string('kind', 16);                              // process | extension | port | artifact | adapter | device
            $t->string('severity', 16);
            $t->string('description')->nullable();
            $t->json('definition');
            $t->boolean('enabled')->default(true);
            $t->json('actions')->nullable();                     // per-mode action overrides {test:[...], practice:[...]}
            $t->timestamps();
        });

        Schema::create('signature_versions', function (Blueprint $t) {
            $t->id();
            $t->string('version', 64)->unique();
            $t->longText('bundle');                              // canonical JSON as sent to agents
            $t->text('signature');                               // base64, signed with the CA key
            $t->string('sha256', 64);
            $t->foreignId('created_by')->nullable()->constrained('users')->nullOnDelete();
            $t->string('note')->nullable();
            $t->boolean('active')->default(false);
            $t->timestamps();
        });

        Schema::create('blocklist_entries', function (Blueprint $t) {
            $t->id();
            $t->string('category', 24);                          // ai | ide | marketplace | doh | vpn | custom
            $t->string('value', 255);                            // domain or host/path rule
            $t->string('kind', 8)->default('domain');            // domain | path | ip
            $t->string('source', 32)->default('seed');           // seed | manual | import:<name>
            $t->string('note')->nullable();
            $t->boolean('enabled')->default(true);
            $t->foreignId('created_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamps();
            $t->unique(['category', 'value']);
        });
    }

    public function down(): void
    {
        foreach (['blocklist_entries', 'signature_versions', 'rules', 'timetable_entries', 'session_pcs', 'lab_sessions', 'profiles'] as $t) {
            Schema::dropIfExists($t);
        }
    }
};
