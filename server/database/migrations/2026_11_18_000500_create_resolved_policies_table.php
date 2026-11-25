<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;

// S-23 (DECISIONS #26): resolved agent policy per PC, valid while the global policy generation is unchanged.
return new class extends Migration
{
    public function up(): void
    {
        Schema::create('policy_generation', function (Blueprint $t) {
            $t->unsignedTinyInteger('id')->primary();
            $t->unsignedBigInteger('generation');
        });
        DB::table('policy_generation')->insert(['id' => 1, 'generation' => 1]);

        Schema::create('resolved_policies', function (Blueprint $t) {
            $t->id();
            $t->foreignId('pc_id')->unique()->constrained('pcs')->cascadeOnDelete();
            $t->unsignedBigInteger('generation');
            $t->string('stamp', 64);
            $t->unsignedBigInteger('version');
            $t->json('policy');
            $t->timestamps();
            $t->index(['pc_id', 'version']);
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('resolved_policies');
        Schema::dropIfExists('policy_generation');
    }
};
