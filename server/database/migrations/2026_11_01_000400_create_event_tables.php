<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        // High-volume table. On MySQL see ccc-deploy/mysql/partition-events.sql for monthly
        // RANGE partitioning; retention purges whole days via PurgeRetention.
        Schema::create('events', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->unsignedBigInteger('agent_id');
            $t->unsignedBigInteger('seq');
            $t->foreignId('lab_session_id')->nullable();
            $t->string('type', 32);
            $t->timestamp('occurred_at', 3);                     // agent time corrected by clock offset
            $t->timestamp('received_at', 3)->useCurrent();
            $t->string('user', 64)->nullable();
            $t->json('data');
            $t->unique(['agent_id', 'seq']);                     // de-duplication of re-sent batches
            $t->index(['pc_id', 'occurred_at']);
            $t->index(['lab_session_id', 'type']);
        });

        Schema::create('alerts', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('lab_session_id')->nullable();
            $t->unsignedBigInteger('event_id')->nullable();
            $t->string('rule_id', 64);
            $t->string('severity', 16);
            $t->string('category', 32)->nullable();
            $t->string('title', 200);
            $t->json('details')->nullable();
            $t->string('user', 64)->nullable();
            $t->timestamp('occurred_at', 3);
            $t->foreignId('acknowledged_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamp('acknowledged_at')->nullable();
            $t->text('comment')->nullable();
            $t->timestamps();
            $t->index(['lab_session_id', 'severity']);
            $t->index(['pc_id', 'occurred_at']);
        });

        Schema::create('key_stats', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('lab_session_id')->nullable();
            $t->string('user', 64)->nullable();
            $t->timestamp('window_start', 3);
            $t->unsignedSmallInteger('window_s')->default(5);
            $t->unsignedInteger('keys')->default(0);
            $t->unsignedInteger('backspaces')->default(0);
            $t->float('iki_mean')->nullable();
            $t->float('iki_median')->nullable();
            $t->float('iki_p90')->nullable();
            $t->float('iki_std')->nullable();
            $t->unsignedSmallInteger('bursts')->default(0);
            $t->unsignedInteger('max_pause')->nullable();
            $t->unsignedSmallInteger('paste_chords')->default(0);
            $t->index(['lab_session_id', 'pc_id', 'window_start']);
        });

        Schema::create('paste_events', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('lab_session_id')->nullable();
            $t->string('user', 64)->nullable();
            $t->boolean('is_copy')->default(false);
            $t->unsignedInteger('chars');
            $t->unsignedInteger('lines');
            $t->boolean('looks_like_code')->default(false);
            $t->float('code_score')->default(0);
            $t->string('source_app', 64)->nullable();
            $t->string('target_app', 64)->nullable();
            $t->timestamp('occurred_at', 3);
            $t->index(['lab_session_id', 'pc_id']);
        });

        Schema::create('net_events', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->nullable()->constrained()->nullOnDelete();
            $t->foreignId('lab_session_id')->nullable();
            $t->string('client_ip', 45);
            $t->string('host', 255);
            $t->string('path', 512)->nullable();
            $t->string('layer', 16);                             // dns | sni | http | path | search-ai
            $t->string('action', 16);                            // blocked | allowed | rewritten
            $t->string('category', 24)->nullable();
            $t->string('matched', 255)->nullable();
            $t->timestamp('occurred_at', 3);
            $t->index(['lab_session_id', 'action']);
            $t->index(['pc_id', 'occurred_at']);
        });

        Schema::create('scores', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_session_id')->constrained()->cascadeOnDelete();
            $t->foreignId('pc_id')->constrained()->cascadeOnDelete();
            $t->foreignId('student_id')->nullable();
            $t->string('user', 64)->nullable();
            $t->string('method', 16)->default('v1');            // v1 (rules) | v2 (isolation forest)
            $t->unsignedTinyInteger('score');
            $t->string('confidence', 16)->nullable();            // low | medium | high
            $t->json('reasons');                                  // ranked, each traceable to a signal
            $t->json('features')->nullable();
            $t->json('not_observable')->nullable();
            $t->timestamps();
            $t->unique(['lab_session_id', 'pc_id', 'user', 'method']);
        });

        Schema::create('reports', function (Blueprint $t) {
            $t->id();
            $t->foreignId('lab_session_id')->nullable()->constrained()->nullOnDelete();
            $t->foreignId('student_id')->nullable();
            $t->string('kind', 16);                              // session | student | research
            $t->string('status', 16)->default('queued');         // queued | ready | failed
            $t->json('summary')->nullable();
            $t->string('path')->nullable();
            $t->foreignId('requested_by')->nullable()->constrained('users')->nullOnDelete();
            $t->timestamps();
        });
    }

    public function down(): void
    {
        foreach (['reports', 'scores', 'net_events', 'paste_events', 'key_stats', 'alerts', 'events'] as $t) {
            Schema::dropIfExists($t);
        }
    }
};
