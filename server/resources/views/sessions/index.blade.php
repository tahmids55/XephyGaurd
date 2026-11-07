@extends('layouts.app')
@section('title', 'Sessions')
@section('content')
<div class="grid gap-4 lg:grid-cols-2">
  @if (isset($preflight))
    <div class="card lg:col-span-2 space-y-2">
      <div class="flex flex-wrap items-center justify-between gap-2">
        <div class="flex items-center gap-2">
          <h2 class="font-semibold">Session Pre-Flight Check</h2>
          <span class="badge {{ $preflight['ready'] ? 'bg-emerald-100 text-emerald-800' : 'bg-amber-100 text-amber-800' }}">
            {{ $preflight['ready'] ? 'READY' : 'ATTENTION NEEDED' }}
          </span>
        </div>
        <div class="text-xs text-slate-600">
          Online: <b>{{ $preflight['pcs_online'] }}/{{ $preflight['pcs_total'] }}</b>
          · Agents Current: <b>{{ $preflight['agents_current'] }}</b> ({{ $preflight['agents_outdated'] }} outdated)
          · CA: <b>{{ $preflight['ca_healthy'] ? 'Healthy' : 'Degraded' }}</b>
          · Signatures: <code class="font-mono">{{ $preflight['active_signature_version'] ?? 'none' }}</code>
          · Isolated: <b>{{ $preflight['isolated_pcs'] }}</b>
        </div>
      </div>
      <div class="grid gap-2 sm:grid-cols-5 text-xs">
        @foreach ($preflight['checks'] as $chk)
          <div class="rounded border p-2 {{ $chk['ok'] ? 'border-emerald-200 bg-emerald-50 text-emerald-900' : 'border-amber-200 bg-amber-50 text-amber-900' }}">
            <div class="font-semibold">{{ $chk['check'] }}</div>
            <div>{{ $chk['detail'] }}</div>
          </div>
        @endforeach
      </div>
    </div>
  @endif
  <div class="card space-y-3">
    <h2 class="font-semibold">Current session</h2>
    @if ($active)
      <div><span class="badge mode-{{ $active->mode }}">{{ $active->mode }}</span> <b>{{ $active->course }}</b> — {{ $active->profile->name }}, started {{ $active->started_at->format('H:i') }}</div>
      <form method="post" action="{{ route('sessions.stop', $active) }}">@csrf<button class="btn btn-danger">End session</button></form>
      @if (auth()->user()->hasRole('instructor'))
        <h3 class="mt-4 text-sm font-semibold">Per-PC overrides (accommodations) — always audited</h3>
        <table class="tbl"><tr><th>PC</th><th>Override profile</th><th>Reason</th><th></th></tr>
          @foreach ($active->sessionPcs as $sp)
            <tr><form method="post" action="{{ route('sessions.override', [$active, $sp->pc]) }}">@csrf
              <td>{{ $sp->pc->name }}</td>
              <td><select name="profile_id" class="input"><option value="">— session profile —</option>
                @foreach ($profiles as $p)<option value="{{ $p->id }}" @selected($sp->override_profile_id === $p->id)>{{ $p->name }}</option>@endforeach</select></td>
              <td><input name="reason" class="input" value="{{ $sp->override_reason }}" placeholder="required" required></td>
              <td><button class="btn btn-sm">Save</button></td></form></tr>
          @endforeach
        </table>
      @endif
    @else
      <form method="post" action="{{ route('sessions.start') }}" class="grid grid-cols-2 gap-3">@csrf
        <div><label class="label">Course</label><input name="course" class="input" required></div>
        <div><label class="label">Profile</label><select name="profile_id" class="input">@foreach ($profiles as $p)<option value="{{ $p->id }}">{{ $p->name }}</option>@endforeach</select></div>
        <div><label class="label">Duration (minutes, optional)</label><input name="duration_min" type="number" class="input"></div>
        <div><label class="label">Experiment condition (research)</label><select name="experiment_condition" class="input"><option value="">—</option><option>baseline</option><option>treatment</option></select></div>
        <div class="col-span-2"><button class="btn btn-primary">Start session</button></div>
      </form>
    @endif
  </div>
  <div class="card">
    <h2 class="mb-2 font-semibold">Broadcast message</h2>
    <form method="post" action="{{ route('sessions.message') }}" class="flex gap-2">@csrf
      <input name="text" class="input" placeholder="e.g. 10 minutes left — save your work" required><button class="btn btn-primary">Send to all</button></form>
    <h2 class="mt-5 mb-2 font-semibold">Recent commands</h2>
    <table class="tbl"><tr><th>Time</th><th>PC</th><th>Command</th><th>Status</th></tr>
      @foreach ($commands as $c)<tr><td class="font-mono text-xs">{{ $c->created_at->format('H:i:s') }}</td><td>{{ $c->pc?->name }}</td><td>{{ $c->type }}</td>
        <td><span class="badge {{ $c->status === 'ok' ? 'bg-emerald-100 text-emerald-800' : 'sev-info' }}">{{ $c->status }}</span></td></tr>@endforeach
    </table>
  </div>
  <div class="card lg:col-span-2">
    <h2 class="mb-2 font-semibold">History</h2>
    <table class="tbl"><tr><th>Start</th><th>End</th><th>Course</th><th>Profile</th><th>Status</th><th>Condition</th><th></th></tr>
      @foreach ($history as $s)
        <tr><td>{{ $s->starts_at->format('M d H:i') }}</td><td>{{ $s->ended_at?->format('H:i') }}</td><td>{{ $s->course }}</td><td>{{ $s->profile?->name }}</td>
          <td>{{ $s->status }}</td><td>{{ $s->experiment_condition }}</td>
          <td>@if ($s->status === 'ended' && auth()->user()->hasRole('instructor'))<a class="text-indigo-600" href="{{ route('reports.show', $s) }}">Report</a>@endif</td></tr>
      @endforeach
    </table>{{ $history->links() }}
  </div>
</div>
@endsection
