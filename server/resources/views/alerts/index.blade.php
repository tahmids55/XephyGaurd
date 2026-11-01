@extends('layouts.app')
@section('title', 'Alerts')
@section('content')
<div class="card">
  <form class="mb-3 flex flex-wrap items-end gap-2">
    <div><label class="label">Session</label><select name="session" class="input w-64"><option value="">All</option>
      @foreach ($sessions as $s)<option value="{{ $s->id }}" @selected(($filters['session'] ?? '') == $s->id)>{{ $s->starts_at->format('M d H:i') }} · {{ $s->course }} ({{ $s->mode }})</option>@endforeach</select></div>
    <div><label class="label">Severity</label><select name="severity" class="input w-36"><option value="">All</option>
      @foreach (['critical', 'high', 'medium', 'low', 'info'] as $sv)<option @selected(($filters['severity'] ?? '') === $sv)>{{ $sv }}</option>@endforeach</select></div>
    <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="open" value="1" @checked($filters['open'] ?? false)> Unacknowledged only</label>
    <button class="btn">Filter</button>
  </form>
  <table class="tbl">
    <tr><th>Time</th><th>Severity</th><th>PC</th><th>User</th><th>Alert</th><th>Rule</th><th>Review</th></tr>
    @forelse ($alerts as $a)
      <tr>
        <td class="font-mono text-xs whitespace-nowrap">{{ $a->occurred_at->format('M d H:i:s') }}</td>
        <td><span class="badge sev-{{ $a->severity }}">{{ $a->severity }}</span></td>
        <td>{{ $a->pc?->name }}</td><td>{{ $a->user }}</td><td>{{ $a->title }}</td>
        <td class="font-mono text-xs">{{ $a->rule_id }}</td>
        <td class="w-80">
          @if ($a->acknowledged_at)
            <div class="text-xs text-slate-500">✓ {{ $a->acknowledger?->name }} {{ $a->acknowledged_at->diffForHumans() }}</div>
            @if ($a->comment)<div class="text-xs">“{{ $a->comment }}”</div>@endif
          @else
            <form method="post" action="{{ route('alerts.ack', $a) }}" class="flex gap-1">@csrf
              <input name="comment" class="input" placeholder="Comment (optional)"><button class="btn btn-sm">Ack</button></form>
          @endif
        </td>
      </tr>
    @empty
      <tr><td colspan="7" class="py-6 text-center text-slate-500">No alerts.</td></tr>
    @endforelse
  </table>
  {{ $alerts->links() }}
  <p class="mt-3 text-xs text-slate-500">Every alert is a flag for human review. XephyGuard never decides on misconduct.</p>
</div>
@endsection
