<?php

namespace Database\Seeders;

use App\Services\Bootstrap;
use Illuminate\Database\Seeder;

class DatabaseSeeder extends Seeder
{
    public function run(Bootstrap $bootstrap): void
    {
        $bootstrap->run();
        $this->call(Phase0PolicySeeder::class);
    }
}
