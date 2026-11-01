<?php

namespace App\Events;

use App\Models\Alert;
use Illuminate\Broadcasting\PrivateChannel;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;

class AlertRaised implements ShouldBroadcastNow
{
    public array $payload;
    public int $labId;

    public function __construct(Alert $alert)
    {
        $this->payload = $alert->broadcastPayload();
        $this->labId = $alert->pc->lab_id;
    }

    public function broadcastOn(): array { return [new PrivateChannel('lab.'.$this->labId)]; }
    public function broadcastAs(): string { return 'alert.raised'; }
    public function broadcastWith(): array { return $this->payload; }
}
