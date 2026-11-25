<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Command;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F24 / DECISIONS #31: the open Programs tab holds a short process lease; renewals are throttled per PC. */
class ProcessLeaseTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_lease_sends_one_command_per_pc_per_window(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        [$other] = $this->enrolledPc('192.168.10.22', 'PC-02');
        $this->actingAs($this->user('assistant'));

        $this->postJson("/pcs/{$pc->id}/process-lease")->assertOk()->assertJson(['sent' => true]);
        $this->postJson("/pcs/{$pc->id}/process-lease")->assertOk()->assertJson(['sent' => false]);
        $this->postJson("/pcs/{$other->id}/process-lease")->assertOk()->assertJson(['sent' => true]);

        $cmds = Command::where('type', 'process_lease')->orderBy('id')->get();
        $this->assertCount(2, $cmds);
        $this->assertSame(['ttl_s' => 20], $cmds[0]->args);
    }

    public function test_lease_needs_login(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->postJson("/pcs/{$pc->id}/process-lease")->assertUnauthorized();
        $this->assertSame(0, Command::count());
    }
}
