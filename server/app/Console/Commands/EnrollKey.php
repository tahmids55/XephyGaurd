<?php

namespace App\Console\Commands;

use App\Models\Lab;
use App\Services\Bootstrap;
use App\Services\CertificateAuthority;
use App\Services\Doctor;
use App\Services\NetworkPolicyWriter;
use App\Services\SignatureCompiler;
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
