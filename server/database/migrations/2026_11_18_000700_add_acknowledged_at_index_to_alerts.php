<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-32 (I-SERVER-41): every page counts open alerts with whereNull('acknowledged_at').
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('alerts', function (Blueprint $t) {
            $t->index('acknowledged_at');
        });
    }

    public function down(): void
    {
        Schema::table('alerts', function (Blueprint $t) {
            $t->dropIndex(['acknowledged_at']);
        });
    }
};
