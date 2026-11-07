<?php

namespace App\Console\Commands;

use App\Services\Audit;
use App\Services\EthicsPackGenerator;
use Illuminate\Console\Command;

class EthicsPackCommand extends Command
{
    protected $signature = 'xephyguard:ethics-pack {--output= : Optional file path to write the Markdown DPIA & IRB Ethics-Review Pack}';

    protected $description = 'Generate a complete Markdown Data Protection Impact Assessment (DPIA) & IRB Ethics-Review Pack from live lab settings';

    public function handle(EthicsPackGenerator $generator): int
    {
        $data = $generator->buildData();
        $markdown = $generator->toMarkdown($data);
        $outputPath = $this->option('output');

        if (is_string($outputPath) && $outputPath !== '') {
            @mkdir(dirname($outputPath), 0755, true);
            file_put_contents($outputPath, $markdown);
            Audit::log('ethics_pack.generate', null, ['output' => $outputPath, 'lab_name' => $data['lab_name']]);
            $this->info("Ethics-Review & DPIA Pack written to {$outputPath}");

            return self::SUCCESS;
        }

        Audit::log('ethics_pack.generate', null, ['output' => 'stdout', 'lab_name' => $data['lab_name']]);
        $this->line($markdown);

        return self::SUCCESS;
    }
}
