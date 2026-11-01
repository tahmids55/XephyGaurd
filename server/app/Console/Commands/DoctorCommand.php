<?php

namespace App\Console\Commands;

use App\Models\Lab;
use App\Services\Bootstrap;
use App\Services\CertificateAuthority;
use App\Services\Doctor;
use App\Services\NetworkPolicyWriter;
use App\Services\SignatureCompiler;
use Illuminate\Console\Command;

class DoctorCommand extends Command
{
    protected $signature = 'xephyguard:doctor {--json}';
    protected $description = 'Check the CCC and explain problems in plain language';

    public function handle(Doctor $d): int
    {
        $res = $d->run();
        if ($this->option('json')) {
            $this->line(json_encode($res, JSON_PRETTY_PRINT));
        } else {
            foreach ($res as $r) {
                $mark = $r['ok'] ? '<info>✔</info>' : ($r['level'] === 'error' ? '<error>✘</error>' : '<comment>!</comment>');
                $this->line("$mark {$r['check']}: {$r['detail']}");
                if (! $r['ok'] && $r['fix']) {
                    $this->line("    → {$r['fix']}");
                }
            }
        }

        return collect($res)->contains(fn ($r) => $r['level'] === 'error') ? self::FAILURE : self::SUCCESS;
    }
}
