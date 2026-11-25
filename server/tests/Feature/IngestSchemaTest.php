<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Agent;
use App\Models\Event;
use App\Services\Ingest\EventSchema;
use App\Services\SessionManager;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-28: per-type allow-list; unknown or mistyped fields never reach the database, strings are cut. */
class IngestSchemaTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function ingest(array $events, array $hb = []): void
    {
        $lab = $this->bootLab();
        [$pc, $token, $cert] = $this->enrolledPc();
        app(SessionManager::class)->start($lab, $this->profile('Test: AI blocked'), 'CS101');
        $this->withHeaders($this->agentHeaders($token, $cert))->postJson('/api/agent/v1/events', ['v' => 1,
            'pc_id' => (string) $pc->id, 'heartbeat' => $hb + ['mode' => 'test', 'user' => 'alice',
                'local_time_ms' => now()->getTimestampMs()], 'events' => $events])->assertOk();
    }

    private function data(string $type): array
    {
        return Event::query()->where('type', $type)->firstOrFail()->data;
    }

    public function test_unknown_fields_are_dropped_and_strings_cut(): void
    {
        $ts = now()->getTimestampMs();
        $this->ingest([
            ['seq' => 1, 'ts' => $ts, 'type' => 'rule_hit', 'data' => ['rule_id' => 'proc.ollama', 'severity' => 'high',
                'category' => 'local_llm', 'description' => str_repeat('d', 2000), 'subject' => ['not', 'a', 'string'],
                'actions' => ['log', 'alert', 7], 'clipboard_text' => 'secret paste', 'user' => 'alice',
                'context' => ['exe' => 'ollama', 'pid' => 42, 'typed' => 'hunter2', 'nested' => ['x' => 1]],
                'result' => ['kill' => ['pid' => 42, 'killed' => true, 'screen' => 'pixels']]]],
            ['seq' => 2, 'ts' => $ts, 'type' => 'process_snapshot', 'data' => ['count' => 1, 'hash' => 'h',
                'list' => [['pid' => 42, 'exe' => 'ollama', 'cmdline' => 'ollama run --token abc'], 'junk'],
                'added' => ['ollama', ['x']]]],
            ['seq' => 3, 'ts' => $ts, 'type' => 'paste', 'data' => ['chars' => 10, 'lines' => 1, 'text' => 'the paste',
                'code' => 'yes']],
        ]);

        $hit = $this->data('rule_hit');
        $this->assertArrayNotHasKey('clipboard_text', $hit);
        $this->assertArrayNotHasKey('subject', $hit);          // wrong type: dropped, not cast
        $this->assertSame(512, mb_strlen($hit['description']));
        $this->assertSame(['log', 'alert'], $hit['actions']);
        $this->assertSame(['exe' => 'ollama', 'pid' => 42], $hit['context']);
        $this->assertSame(['kill' => ['pid' => 42, 'killed' => true]], $hit['result']);
        $snap = $this->data('process_snapshot');
        $this->assertSame([['pid' => 42, 'exe' => 'ollama']], $snap['list']);
        $this->assertSame(['ollama'], $snap['added']);
        $this->assertSame(['chars' => 10, 'lines' => 1], $this->data('paste'));
        $this->assertDatabaseHas('alerts', ['rule_id' => 'proc.ollama']);   // derived rows still work
    }

    public function test_heartbeat_strings_are_typed_and_limited(): void
    {
        $this->ingest([], ['agent_version' => str_repeat('9', 200), 'signatures_version' => ['x'], 'mode' => ['test'],
            'user' => str_repeat('u', 300), 'capabilities' => ['keys' => true, 'screen' => ['x11' => true, 'deep' => ['a' => 1]],
                str_repeat('k', 40) => true], 'usage' => ['cpu_pct' => 0.5, 'leak' => 'secret', 'screen' => ['fps' => 3]]]);
        $agent = Agent::query()->firstOrFail();
        $pc = $agent->pc;
        $this->assertSame(32, strlen((string) $agent->version));
        $this->assertNotSame(['x'], $agent->signatures_version);
        $this->assertSame(64, mb_strlen((string) $pc->current_user));
        $this->assertSame(['keys' => true, 'screen' => ['x11' => true]], $pc->capabilities);
        $this->assertArrayNotHasKey('leak', $pc->usage);
        $this->assertSame(0.5, $pc->usage['cpu_pct']);
        $this->assertSame(['fps' => 3], $pc->usage['screen']);
    }

    public function test_every_known_type_has_a_schema(): void
    {
        foreach (['heartbeat', 'agent_started', 'agent_stopping', 'process_start', 'process_stop', 'process_snapshot',
            'rule_hit', 'listening_port', 'window_focus', 'paste', 'copy', 'key_stats', 'net_adapter', 'device',
            'extension_scan', 'artifact_scan', 'file_activity', 'gpu_sample', 'tamper', 'capabilities', 'command_result',
            'helper_status'] as $t) {
            $this->assertArrayHasKey($t, EventSchema::TYPES, $t);
            $this->assertSame(['user' => 'bob'], EventSchema::clean($t, ['user' => 'bob', 'zzz_unknown' => 1]), $t);
        }
        $this->assertCount(22, EventSchema::TYPES);
    }
}
