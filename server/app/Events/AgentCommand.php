<?php

namespace App\Events;

use App\Models\Command;
use Illuminate\Broadcasting\PrivateChannel;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;

/** Delivered to one agent on private-pc.{id}; the agent replies with a command_result event. */
class AgentCommand implements ShouldBroadcastNow
{
    public function __construct(public Command $command) {}

    public function broadcastOn(): array
    {
        $channels = [new PrivateChannel('pc.'.$this->command->pc_id)];
        if ($this->command->pc) {
            $otherPcIds = \App\Models\Pc::query()
                ->where('hostname', $this->command->pc->hostname)
                ->where('id', '!=', $this->command->pc_id)
                ->pluck('id');
            foreach ($otherPcIds as $oid) {
                $channels[] = new PrivateChannel('pc.'.$oid);
            }
        }

        return $channels;
    }
    public function broadcastAs(): string { return 'command'; }
    public function broadcastWith(): array
    {
        return ['id' => (string) $this->command->id, 'type' => $this->command->type, 'args' => (object) ($this->command->args ?? [])];
    }
}
