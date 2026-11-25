<?php

namespace App\Console\Commands;

use App\Services\NetworkPolicyWriter;
use Illuminate\Console\Command;

class NetWrite extends Command
{
    protected $signature = 'xephyguard:net:write';

    protected $description = 'Regenerate the network policy state for DNS, nftables and the proxy';

    public function handle(NetworkPolicyWriter $w): int
    {
        $s = $w->write();
        $this->info("state v{$s['version']}: ".collect($s['groups'])->map(fn ($g, $k) => "$k=".count($g))->implode(' '));

        return self::SUCCESS;
    }
}
