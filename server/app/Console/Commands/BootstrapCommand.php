<?php

namespace App\Console\Commands;

use App\Services\Bootstrap;
use App\Services\NetworkPolicyWriter;
use Illuminate\Console\Command;

class BootstrapCommand extends Command
{
    protected $signature = 'xephyguard:bootstrap';

    protected $description = 'Seed lab, built-in profiles, blocklists and rules; publish signatures; write network state';

    public function handle(Bootstrap $b, NetworkPolicyWriter $net): int
    {
        foreach ($b->run() as $line) {
            $this->line(' - '.$line);
        }
        $net->write();
        $this->info('Network policy written to '.config('xephyguard.net_state_path'));

        return self::SUCCESS;
    }
}
