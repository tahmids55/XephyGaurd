<?php

use Illuminate\Support\Facades\Broadcast;

// Dashboard users: live lab view.
Broadcast::channel('lab.{labId}', fn ($user) => $user->active && $user->hasRole('assistant', 'instructor'));

// Agent channels (private-pc.{id}) are authorized by /api/agent/v1/broadcasting/auth with the
// agent's own credentials; dashboard users never subscribe to them.
Broadcast::channel('pc.{pcId}', fn () => false);
