@extends('layouts.app')
@section('title', 'Process & AI Disclosure Work Journal')
@section('content')
<div class="mx-auto max-w-4xl space-y-4">
  <div class="card flex flex-wrap items-center justify-between gap-4">
    <div>
      <h1 class="text-xl font-bold">Process & AI Disclosure Work Journal</h1>
      <p class="text-sm text-slate-600">
        Session: <b>{{ $session['course'] }}</b> (#{{ $session['id'] }})
        · PC: <b>{{ $pc['name'] }}</b>
        · AI Policy: <span class="badge sev-info">{{ $session['ai_policy'] }}</span>
        @if ($student)
          · Student: <b>{{ $student['name'] }}</b> ({{ $student['student_number'] }})
        @endif
      </p>
    </div>
    <a href="{{ route('reports.show', $session['id']) }}" class="btn btn-sm">Back to Session Report</a>
  </div>

  <div class="grid gap-4 sm:grid-cols-2">
    <div class="card">
      <div class="text-xs text-slate-500">Incremental Work Folder Saves</div>
      <div class="text-2xl font-bold">{{ $file_saves_count }}</div>
    </div>
    <div class="card">
      <div class="text-xs text-slate-500">Disclosed AI Tool Activities</div>
      <div class="text-2xl font-bold">{{ $disclosed_ai_count }}</div>
    </div>
  </div>

  <div class="card space-y-3">
    <h2 class="font-semibold">Chronological Process & Disclosure Timeline</h2>
    @if (empty($entries))
      <p class="text-sm text-slate-500">No work-folder saves or disclosed AI events recorded for this PC in this session.</p>
    @else
      <table class="tbl">
        <tr>
          <th>Time</th>
          <th>Kind</th>
          <th>Class & Confidence</th>
          <th>Summary</th>
          <th>Evidence Explanation</th>
        </tr>
        @foreach ($entries as $entry)
          <tr>
            <td class="font-mono text-xs">{{ $entry['occurred_at'] }}</td>
            <td><span class="badge {{ $entry['kind'] === 'work_progress' ? 'bg-emerald-100 text-emerald-800' : 'sev-info' }}">{{ $entry['kind'] }}</span></td>
            <td>{{ $entry['event_class'] }} ({{ $entry['confidence'] }})</td>
            <td class="font-medium">{{ $entry['summary'] }}</td>
            <td class="text-xs text-slate-600">{{ $entry['evidence_meta']['what_it_means'] ?? '' }}</td>
          </tr>
        @endforeach
      </table>
    @endif
  </div>
</div>
@endsection
