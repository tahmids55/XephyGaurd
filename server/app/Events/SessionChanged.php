<?php

namespace App\Events;

use App\Models\LabSession;
use Illuminate\Broadcasting\PrivateChannel;
use Illuminate\Contracts\Broadcasting\ShouldBroadcastNow;

class SessionChanged implements ShouldBroadcastNow
{
    public array $payload;
    public int $labId;

    public function __construct(LabSession $s)
    {
        $this->labId = $s->lab_id;
        $this->payload = ['id' => $s->id, 'status' => $s->status, 'mode' => $s->mode, 'course' => $s->course,
            'profile' => $s->profile?->name];
    }

    public function broadcastOn(): array { return [new PrivateChannel('lab.'.$this->labId)]; }
    public function broadcastAs(): string { return 'session.changed'; }
    public function broadcastWith(): array { return $this->payload; }
}
