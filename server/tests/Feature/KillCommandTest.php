<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Command;
use App\Services\CommandBus;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Validation\ValidationException;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F35: the kill command carries the process start time so the agent never hits a reused pid. */
class KillCommandTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_kill_by_pid_forwards_start_time_and_exe_kill_does_not(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->actingAs($this->user('assistant'));
        $this->processSnapshot($pc, [['pid' => 77, 'exe' => 'ollama', 'start' => 123456]]);

        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 77, 'start_time' => 123456])->assertOk();
        $this->postJson("/pcs/{$pc->id}/kill", ['exe' => 'cursor', 'start_time' => 99])->assertOk();
        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 77, 'start_time' => 'x'])->assertStatus(422);

        $args = Command::where('type', 'kill')->orderBy('id')->pluck('args')->all();
        $this->assertSame(['pid' => 77, 'start_time' => 123456], $args[0]);
        $this->assertSame(['exe' => 'cursor'], $args[1]);  // start_time only makes sense with a pid
    }

    public function test_kill_pid_must_be_valid_and_in_the_latest_snapshot(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        [$other] = $this->enrolledPc('192.168.10.22', 'PC-02');
        $this->actingAs($this->user('assistant'));
        $this->processSnapshot($pc, [['pid' => 900, 'exe' => 'old', 'start' => 1]], 1);
        $this->processSnapshot($pc, [['pid' => 77, 'exe' => 'ollama', 'start' => 500]], 2);
        $this->processSnapshot($other, [['pid' => 4242, 'exe' => 'ollama', 'start' => 9]], 1);

        foreach ([0, 1, -5] as $pid) {
            $this->postJson("/pcs/{$pc->id}/kill", ['pid' => $pid])->assertStatus(422)->assertJsonValidationErrors('pid');
        }
        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 900])->assertStatus(422);              // only in an older snapshot
        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 4242])->assertStatus(422);             // another PC's process
        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 77, 'start_time' => 499])->assertStatus(422);   // reused pid
        $this->assertSame(0, Command::where('type', 'kill')->count());

        $this->postJson("/pcs/{$pc->id}/kill", ['pid' => 77, 'start_time' => 500])->assertOk();
        $this->assertSame(1, Command::where('type', 'kill')->count());
    }

    public function test_every_command_type_has_its_own_argument_rules(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $bus = app(CommandBus::class);
        $this->assertSame(['what' => 'ports'], $bus->send($pc, 'rescan', ['what' => 'ports', 'extra' => 'x'])->args);
        $this->assertSame([], $bus->send($pc, 'ping', ['anything' => 1])->args);
        $this->assertSame(['isolated' => true], $bus->send($pc, 'isolate_notice', ['isolated' => true])->args);
        foreach ([['rescan', ['what' => 'everything']], ['message', ['text' => str_repeat('x', 501)]], ['message', []],
            ['screen_lease', ['ttl_s' => 0]], ['isolate_notice', []], ['kill', []]] as [$type, $args]) {
            try {
                $bus->send($pc, $type, $args);
                $this->fail("$type accepted invalid args");
            } catch (ValidationException) {
                $this->addToAssertionCount(1);
            }
        }
        $this->assertSame(3, Command::count());
    }
}
