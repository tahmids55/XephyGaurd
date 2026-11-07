@extends('layouts.app')
@section('title', 'Appeals & Overturn Rate')
@section('content')
<div class="space-y-4">
  <div class="grid gap-4 md:grid-cols-4">
    <div class="card">
      <div class="text-xs text-slate-500">Open Appeals</div>
      <div class="text-2xl font-bold">{{ $counts['open'] }}</div>
    </div>
    <div class="card">
      <div class="text-xs text-slate-500">Decided Total</div>
      <div class="text-2xl font-bold">{{ $decided_total }}</div>
    </div>
    <div class="card">
      <div class="text-xs text-slate-500">Institutional Overturn Rate</div>
      <div class="text-2xl font-bold">{{ number_format($overturn_rate * 100, 1) }}%</div>
      <div class="text-xs text-slate-500">{{ $overturned_count }} overturned / {{ $decided_total }} decided</div>
    </div>
    <div class="card">
      <div class="text-xs text-slate-500">Top Overturned Rules (False-Positive Signal)</div>
      @if (empty($overturned_by_rule))
        <div class="text-xs text-slate-400 mt-1">None</div>
      @else
        <div class="mt-1 space-y-1 text-xs">
          @foreach ($overturned_by_rule as $rule => $cnt)
            <div><code class="font-mono">{{ $rule }}</code>: <b>{{ $cnt }}</b></div>
          @endforeach
        </div>
      @endif
    </div>
  </div>

  <div class="card space-y-3">
    <div class="flex flex-wrap items-center justify-between gap-2">
      <h2 class="font-semibold">Student Appeals Queue</h2>
      <div class="flex gap-1 text-xs">
        <a href="{{ route('appeals.index') }}" class="btn btn-sm {{ $status_filter === '' ? 'btn-primary' : '' }}">All ({{ $counts['total'] }})</a>
        @foreach (['open', 'upheld', 'overturned', 'noted'] as $st)
          <a href="{{ route('appeals.index', ['status' => $st]) }}" class="btn btn-sm {{ $status_filter === $st ? 'btn-primary' : '' }}">{{ ucfirst($st) }} ({{ $counts[$st] }})</a>
        @endforeach
      </div>
    </div>

    <table class="tbl">
      <tr>
        <th>ID</th>
        <th>Student</th>
        <th>Session</th>
        <th>Alert / Rule</th>
        <th>Student Statement</th>
        <th>Status</th>
        <th>Decision</th>
      </tr>
      @foreach ($appeals as $ap)
        <tr>
          <td>#{{ $ap->id }}</td>
          <td>
            <b>{{ $ap->student?->name }}</b>
            <div class="font-mono text-xs text-slate-500">{{ $ap->student?->student_number }}</div>
          </td>
          <td>{{ $ap->session?->course }} (#{{ $ap->lab_session_id }})</td>
          <td>
            @if ($ap->alert)
              <code class="font-mono text-xs">{{ $ap->alert->rule_id }}</code>
              <div class="text-xs text-slate-500">{{ $ap->alert->title }}</div>
            @else
              <span class="text-xs text-slate-500">Session-wide</span>
            @endif
          </td>
          <td class="max-w-md text-xs">{{ $ap->student_statement }}</td>
          <td><span class="badge sev-info">{{ $ap->status }}</span></td>
          <td>
            @if ($ap->status === 'open')
              <form method="post" action="{{ route('appeals.decide', $ap) }}" class="flex flex-wrap items-center gap-1">
                @csrf
                <select name="status" class="input w-28 text-xs" required>
                  <option value="overturned">Overturn</option>
                  <option value="upheld">Uphold</option>
                  <option value="noted">Note</option>
                </select>
                <input name="decision_reason" class="input w-48 text-xs" placeholder="Reason (required)" required>
                <button class="btn btn-sm btn-primary">Decide</button>
              </form>
            @else
              <div class="text-xs"><b>{{ ucfirst($ap->status) }}</b>: {{ $ap->decision_reason }}</div>
              <div class="text-xs text-slate-500">by {{ $ap->decider?->name }} · {{ $ap->decided_at?->format('Y-m-d H:i') }}</div>
            @endif
          </td>
        </tr>
      @endforeach
    </table>
  </div>
</div>
@endsection
