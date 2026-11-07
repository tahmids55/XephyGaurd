@extends('layouts.app')
@section('title', 'Alerts')
@section('content')
<div class="card">
  <div class="mb-3 flex flex-wrap gap-2 text-xs">
    <span class="badge bg-slate-100 text-slate-700">Installed (Inventory): <b>{{ $installed_count ?? 0 }}</b></span>
    <span class="badge bg-amber-100 text-amber-800">Active Use: <b>{{ $active_count ?? 0 }}</b></span>
    <span class="badge bg-indigo-100 text-indigo-800">Blocked Attempts: <b>{{ $blocked_count ?? 0 }}</b></span>
  </div>
  <form class="mb-3 flex flex-wrap items-end gap-2">
    <div><label class="label">Session</label><select name="session" class="input w-64"><option value="">All</option>
      @foreach ($sessions as $s)<option value="{{ $s->id }}" @selected(($filters['session'] ?? '') == $s->id)>{{ $s->starts_at->format('M d H:i') }} · {{ $s->course }} ({{ $s->mode }})</option>@endforeach</select></div>
    <div><label class="label">Severity</label><select name="severity" class="input w-36"><option value="">All</option>
      @foreach (['critical', 'high', 'medium', 'low', 'info'] as $sv)<option @selected(($filters['severity'] ?? '') === $sv)>{{ $sv }}</option>@endforeach</select></div>
    <div><label class="label">Evidence Class</label><select name="event_class" class="input w-40"><option value="">All</option>
      @foreach (['installed', 'active', 'blocked', 'behavioral', 'system'] as $ec)<option value="{{ $ec }}" @selected(($filters['event_class'] ?? '') === $ec)>{{ $ec }}</option>@endforeach</select></div>
    <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="open" value="1" @checked($filters['open'] ?? false)> Unacknowledged only</label>
    <button class="btn">Filter</button>
  </form>
  <table class="tbl">
    <tr><th>Time</th><th>Severity</th><th>Class / Confidence</th><th>PC</th><th>User</th><th>Alert</th><th>Rule</th><th>Review</th></tr>
    @forelse ($alerts as $a)
      @php($meta = $a->evidence_meta)
      <tr>
        <td class="font-mono text-xs whitespace-nowrap">{{ $a->occurred_at->format('M d H:i:s') }}</td>
        <td><span class="badge sev-{{ $a->severity }}">{{ $a->severity }}</span></td>
        <td class="text-xs whitespace-nowrap">
          <span class="badge bg-slate-100 text-slate-700">{{ $meta['event_class'] }}</span>
          <span class="badge bg-slate-50 text-slate-500">{{ $meta['confidence'] }}</span>
        </td>
        <td>{{ $a->pc?->name }}</td><td>{{ $a->user }}</td>
        <td>
          <div>{{ $a->title }}</div>
          @if (!empty($a->details['what_it_means'] ?? $a->details['meaning'] ?? $meta['what_it_means']))
            <div class="text-xs text-slate-600">Means: {{ $a->details['what_it_means'] ?? $a->details['meaning'] ?? $meta['what_it_means'] }}</div>
          @endif
          @if (!empty($a->details['what_it_does_not_mean'] ?? $a->details['not_meaning'] ?? $meta['what_it_does_not_mean']))
            <div class="text-xs text-slate-400">Does not mean: {{ $a->details['what_it_does_not_mean'] ?? $a->details['not_meaning'] ?? $meta['what_it_does_not_mean'] }}</div>
          @endif
        </td>
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
      <tr><td colspan="8" class="py-6 text-center text-slate-500">No alerts.</td></tr>
    @endforelse
  </table>
  {{ $alerts->links() }}
  <p class="mt-3 text-xs text-slate-500">Every alert is a flag for human review. XephyGuard never decides on misconduct.</p>
</div>
@endsection
