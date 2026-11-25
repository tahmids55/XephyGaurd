<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-49 (I-SERVER-19): a rule saved in the dashboard is marked edited, so the seed import
// on bootstrap no longer overwrites its definition and description.
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('rules', function (Blueprint $t) {
            $t->boolean('edited')->default(false)->after('enabled');
        });
    }

    public function down(): void
    {
        Schema::table('rules', function (Blueprint $t) {
            $t->dropColumn('edited');
        });
    }
};
