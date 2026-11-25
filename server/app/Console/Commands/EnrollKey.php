<?php

namespace App\Console\Commands;

use App\Models\Lab;
use Illuminate\Console\Command;

class EnrollKey extends Command
{
    protected $signature = 'xephyguard:enroll-key {--rotate}';

    protected $description = 'Show (or rotate) the lab enrollment key';

    public function handle(): int
    {
        $lab = Lab::current();
        $this->line($this->option('rotate') ? $lab->rotateEnrollKey() : ($lab->enrollKey() ?? $lab->rotateEnrollKey()));

        return self::SUCCESS;
    }
}
