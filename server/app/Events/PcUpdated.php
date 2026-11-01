<?php

namespace App\Events;

use App\Models\Pc;
use Illuminate\Broadcasting\PrivateChannel;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;

class PcUpdated implements ShouldBroadcastNow
{
    public array $tile;

    public function __construct(Pc $pc) { $this->tile = $pc->tile(); $this->labId = $pc->lab_id; }
    public int $labId;

    public function broadcastOn(): array { return [new PrivateChannel('lab.'.$this->labId)]; }
    public function broadcastAs(): string { return 'pc.updated'; }
    public function broadcastWith(): array { return $this->tile; }
}
