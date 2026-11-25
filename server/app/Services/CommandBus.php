<?php

declare(strict_types=1);

namespace App\Services;

use App\Events\AgentCommand;
use App\Models\Command;
use App\Models\Event;
use App\Models\Pc;
use App\Models\User;
use Illuminate\Support\Facades\Validator;
use Illuminate\Validation\ValidationException;

/**
 * Commands to agents travel over the agent's private Reverb channel (private-pc.{id}).
 * Every type has its own argument rules (S-30, docs/PROTOCOL.md "Commands"): unknown arguments are dropped,
 * invalid ones are refused with 422 before anything is stored or sent.
 */
class CommandBus
{
    public const TYPES = ['policy_updated', 'message', 'kill', 'isolate_notice', 'rescan', 'ping', 'flush', 'screen_lease'];

    public const RESCAN_TARGETS = ['processes', 'network', 'ports', 'devices', 'extensions', 'artifacts', 'all'];

    /** @return array<string, array<string, string|array<int, string>>> */
    private static function rules(): array
    {
        return [
            'policy_updated' => [],
            'message' => ['text' => 'required|string|max:500', 'title' => 'nullable|string|max:120', 'urgent' => 'nullable|boolean'],
            // pid 0/1 and negatives are never valid targets (I-SERVER-40); start_time only together with a pid.
            'kill' => ['exe' => 'required_without:pid|nullable|string|max:128', 'pid' => 'nullable|integer|min:2',
                'start_time' => 'nullable|integer|min:1'],
            'isolate_notice' => ['isolated' => 'required|boolean'],
            'rescan' => ['what' => ['required', 'string', 'in:'.implode(',', self::RESCAN_TARGETS)]],
            'ping' => [],
            'flush' => [],
            'screen_lease' => ['ttl_s' => 'required|integer|min:1|max:3600'],   // the agent's own cap
        ];
    }

    public function send(Pc $pc, string $type, array $args = [], ?User $by = null): Command
    {
        abort_unless(in_array($type, self::TYPES, true), 422, 'unknown command');
        $args = $this->validated($pc, $type, $args);
        $cmd = Command::create(['pc_id' => $pc->id, 'user_id' => $by?->id, 'type' => $type, 'args' => $args]);
        broadcast(new AgentCommand($cmd));

        return $cmd;
    }

    /** @param iterable<Pc> $pcs */
    public function sendMany(iterable $pcs, string $type, array $args = [], ?User $by = null): int
    {
        $n = 0;
        foreach ($pcs as $pc) {
            $this->send($pc, $type, $args, $by);
            $n++;
        }

        return $n;
    }

    /** The allow-listed arguments, or a ValidationException. */
    public function validated(Pc $pc, string $type, array $args): array
    {
        $args = Validator::make($args, self::rules()[$type])->validate();
        if ($type === 'kill' && ! isset($args['pid'])) {
            unset($args['start_time']);
        }
        if ($type === 'kill' && isset($args['pid'])) {
            $this->requireRunning($pc, (int) $args['pid'], isset($args['start_time']) ? (int) $args['start_time'] : null);
        }

        return $args;
    }

    /** A pid kill must target a process in the PC's latest process snapshot (same start time when one is given). */
    private function requireRunning(Pc $pc, int $pid, ?int $start): void
    {
        $snap = Event::query()->where('pc_id', $pc->id)->where('type', 'process_snapshot')->orderByDesc('seq')->first();
        $list = is_array($snap?->data['list'] ?? null) ? $snap->data['list'] : [];
        foreach ($list as $p) {
            if (is_array($p) && ($p['pid'] ?? null) === $pid
                && ($start === null || ! isset($p['start']) || $p['start'] === $start)) {
                return;
            }
        }
        throw ValidationException::withMessages(['pid' => 'This process is not in the PC\'s latest process list.']);
    }
}
