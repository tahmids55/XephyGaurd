<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Rule;
use App\Services\SignatureCompiler;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Str;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-49 (I-SERVER-19): bootstrap's seed import must not overwrite rules edited in the dashboard. */
class SignatureSeedImportTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function seedDir(string $exe, string $description): string
    {
        $dir = sys_get_temp_dir().'/xg-seed-'.Str::random(8);
        mkdir($dir);
        file_put_contents("$dir/processes.yaml", "rules:\n"
            ."  - id: proc.seeda\n    severity: high\n    description: $description\n    match: { exe: [$exe] }\n"
            ."  - id: proc.seedb\n    severity: high\n    description: $description\n    match: { exe: [$exe-b] }\n");

        return $dir;
    }

    public function test_dashboard_edits_survive_a_new_seed_import(): void
    {
        $this->bootLab();
        $compiler = app(SignatureCompiler::class);
        $this->assertSame(2, $compiler->importSeed($this->seedDir('toola', 'Seed v1')));
        $this->assertFalse(Rule::query()->where('rule_id', 'proc.seeda')->value('edited'));

        $this->actingAs($this->user('admin'));
        $this->post('/rules', ['rule_id' => 'proc.seeda', 'kind' => 'process', 'severity' => 'high',
            'description' => 'Edited by staff', 'definition' => json_encode(['match' => ['exe' => ['custom']]])])
            ->assertRedirect();
        $this->assertTrue(Rule::query()->where('rule_id', 'proc.seeda')->first()->edited);

        $this->assertSame(0, $compiler->importSeed($this->seedDir('toolv2', 'Seed v2')));

        $edited = Rule::query()->where('rule_id', 'proc.seeda')->first();
        $this->assertSame('Edited by staff', $edited->description);
        $this->assertSame(['match' => ['exe' => ['custom']]], $edited->definition);

        $untouched = Rule::query()->where('rule_id', 'proc.seedb')->first();
        $this->assertFalse($untouched->edited);
        $this->assertSame('Seed v2', $untouched->description);
        $this->assertSame(['toolv2-b'], $untouched->definition['match']['exe']);
    }

    public function test_bootstrap_seed_import_keeps_an_edited_shipped_rule(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'));
        $this->post('/rules', ['rule_id' => 'proc.ollama', 'kind' => 'process', 'severity' => 'low',
            'description' => 'Allowed in this lab', 'definition' => json_encode(['match' => ['exe' => ['ollama']]])])
            ->assertRedirect();

        $this->assertSame(0, app(SignatureCompiler::class)->importSeed());

        $rule = Rule::query()->where('rule_id', 'proc.ollama')->first();
        $this->assertSame('Allowed in this lab', $rule->description);
        $this->assertSame(['match' => ['exe' => ['ollama']]], $rule->definition);
    }
}
