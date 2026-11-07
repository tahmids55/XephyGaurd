<?php

namespace App\Services;

use App\Events\AgentCommand;
use App\Models\Command;
use App\Models\Pc;
use App\Models\User;

/** Commands to agents travel over the agent's private Reverb channel (private-pc.{id}). */
class CommandBus
{
    public const TYPES = ['policy_updated', 'message', 'kill', 'isolate_notice', 'rescan', 'ping', 'flush'];

    public function send(Pc $pc, string $type, array $args = [], ?User $by = null): Command
    {
        abort_unless(in_array($type, self::TYPES, true), 422, 'unknown command');
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
}
