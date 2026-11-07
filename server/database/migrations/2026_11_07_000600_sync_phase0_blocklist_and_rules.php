<?php

use Database\Seeders\Phase0PolicySeeder;
use Illuminate\Database\Migrations\Migration;
use Illuminate\Support\Facades\Schema;

return new class extends Migration
{
    public function up(): void
    {
        if (Schema::hasTable('blocklist_entries') && Schema::hasTable('rules') && Schema::hasTable('profiles')) {
            Phase0PolicySeeder::sync();
        }
    }

    public function down(): void
    {
        // Idempotent data synchronization; nothing structural to drop.
    }
};
