<?php

declare(strict_types=1);

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

// S-13: new evidence files are Crypt-encrypted at rest; rows from before stay readable as plain files.
return new class extends Migration
{
    public function up(): void
    {
        Schema::table('evidence_captures', function (Blueprint $table) {
            $table->boolean('encrypted')->default(false)->after('size_bytes');
        });
    }

    public function down(): void
    {
        Schema::table('evidence_captures', function (Blueprint $table) {
            $table->dropColumn('encrypted');
        });
    }
};
