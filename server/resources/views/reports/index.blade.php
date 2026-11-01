@extends('layouts.app')
@section('title', 'Reports')
@section('content')
<div class="card">
  <div class="mb-3 flex items-end justify-between">
    <h2 class="text-lg font-semibold">Session reports</h2>
    <form action="{{ route('research.export') }}" class="flex items-end gap-2">
      <div><label class="label">Research export from</label><input type="date" name="from" class="input"></div>
      <div><label class="label">until</label><input type="date" name="until" class="input"></div>
      <button class="btn">Export CSV (consenting, pseudonymized)</button></form>
  </div>
  <table class="tbl"><tr><th>Date</th><th>Course</th><th>Profile</th><th>Report</th><th>Flagged</th><th></th></tr>
    @foreach ($sessions as $s)
      @php($r = $reports[$s->id] ?? null)
      <tr><td>{{ $s->starts_at->format('Y-m-d H:i') }}</td><td>{{ $s->course }}</td><td>{{ $s->profile?->name }}</td>
        <td><span class="badge {{ $r?->status === 'ready' ? 'bg-emerald-100 text-emerald-800' : ($r?->status === 'failed' ? 'sev-high' : 'sev-info') }}">{{ $r?->status ?? 'none' }}</span></td>
        <td>{{ count($r?->summary['flagged'] ?? []) }}</td>
        <td class="flex gap-2"><a class="text-indigo-600" href="{{ route('reports.show', $s) }}">Open</a><a class="text-indigo-600" href="{{ route('reports.pdf', $s) }}">PDF</a><a class="text-indigo-600" href="{{ route('reports.csv', $s) }}">CSV</a></td></tr>
    @endforeach
  </table>{{ $sessions->links() }}
</div>
@endsection
