<?php

namespace App\Console\Commands;

use App\Models\Lab;
use App\Services\Bootstrap;
use App\Services\CertificateAuthority;
use App\Services\Doctor;
use App\Services\NetworkPolicyWriter;
use App\Services\SignatureCompiler;
use Illuminate\Console\Command;

class PublishSignatures extends Command
{
    protected $signature = 'xephyguard:signatures:publish {--import : import new rules from data/signatures first}';
    protected $description = 'Compile, sign and activate the agent signature bundle';

    public function handle(SignatureCompiler $c): int
    {
        if ($this->option('import')) {
            $this->line('imported '.$c->importSeed().' new rules');
        }
        $this->info('active: '.$c->publish(null, 'cli')->version);

        return self::SUCCESS;
    }
}
