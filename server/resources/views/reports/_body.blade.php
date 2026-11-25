{{-- Shared by the HTML report and the PDF export. --}}
<h1 style="margin:0">Integrity report — {{ $session->course }}{{ isset($student) ? ' — '.$student->name : '' }}</h1>
<p>{{ $session->starts_at->format('Y-m-d H:i') }}–{{ $session->ended_at?->format('H:i') }} · profile “{{ $session->profile?->name }}” · mode {{ $session->mode }}
  @if ($session->experiment_condition) · condition {{ $session->experiment_condition }} @endif</p>
<p style="padding:8px;border:1px solid #f59e0b;background:#fffbeb"><b>Flag for human review.</b> Scores summarise observable signals; they are not proof of misconduct.
  Students marked exempt (accommodations) and assignment-provided boilerplate can raise signals legitimately.</p>

<h2>Scores</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>PC</th><th>Student</th><th>Method</th><th>Score</th><th>Confidence</th><th>Reasons</th><th>Not observable</th></tr>
  @forelse (\App\Services\ReportModel::scoreRows($scores) as $r)
    <tr><td>{{ $r['pc'] }}</td><td>{{ $r['student_name'] !== '' ? $r['student_name'] : $r['user'] }}</td><td>{{ $r['method'] }}</td><td><b>{{ $r['score'] }}</b>/100</td><td>{{ $r['confidence'] }}</td>
      <td>@foreach ($r['reasons'] as $x)<div>{{ $x['points'] !== null ? '+'.$x['points'].' ' : '' }}{{ $x['label'] }} @if ($x['signal'] !== '')<small>({{ $x['signal'] }})</small>@endif</div>@endforeach</td>
      <td><small>{{ implode('; ', $r['not_observable']) }}</small></td></tr>
  @empty <tr><td colspan="7">No scores yet{{ isset($report) && $report?->status === 'failed' ? ' (scoring failed: '.($report->summary['error'] ?? '').')' : '' }}.</td></tr>
  @endforelse
</table>

<h2>Alerts ({{ $alerts->count() }})</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>Time</th><th>PC</th><th>User</th><th>Severity</th><th>Alert</th><th>Review</th></tr>
  @foreach ($alerts as $a)<tr><td>{{ $a->occurred_at->format('H:i:s') }}</td><td>{{ $a->pc?->name }}</td><td>{{ $a->user }}</td><td>{{ $a->severity }}</td><td>{{ $a->title }}</td>
    <td>{{ $a->acknowledged_at ? 'acknowledged' : 'open' }} {{ $a->comment }}</td></tr>@endforeach
</table>

<h2>Blocked attempts</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>PC</th><th>Host</th><th>Category</th><th>Attempts</th><th>First</th></tr>
  @foreach ($blocked as $b)<tr><td>{{ $b->pc?->name }}</td><td>{{ $b->host }}</td><td>{{ $b->category }}</td><td>{{ $b->n }}</td><td>{{ \Illuminate\Support\Carbon::parse($b->first_at)->format('H:i:s') }}</td></tr>@endforeach
</table>

<h2>Large pastes (≥ 300 characters)</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>Time</th><th>User</th><th>Chars</th><th>Lines</th><th>Code-like</th><th>From → to</th></tr>
  @foreach ($pastes as $p)<tr><td>{{ $p->occurred_at->format('H:i:s') }}</td><td>{{ $p->user }}</td><td>{{ $p->chars }}</td><td>{{ $p->lines }}</td><td>{{ $p->looks_like_code ? 'yes' : '' }}</td><td>{{ $p->source_app }} → {{ $p->target_app }}</td></tr>@endforeach
</table>
@if (!empty($evidence) && $evidence->count())
<h2>Saved screen evidence ({{ $evidence->count() }})</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>Time</th><th>PC</th><th>User</th><th>Mode</th><th>Captured by</th><th>SHA-256</th></tr>
  @foreach ($evidence as $e)<tr><td>{{ $e->occurred_at?->format('H:i:s') }}</td><td>{{ $e->pc?->name ?? ('PC-' . $e->pc_id) }}</td><td>{{ $e->user ?: '—' }}</td><td>{{ $e->source_mode }} (mon {{ $e->monitor + 1 }})</td><td>{{ $e->capturer?->name ?? '—' }}</td><td><small>{{ substr($e->sha256, 0, 12) }}</small></td></tr>@endforeach
</table>
@endif
<p><small>Generated {{ now()->format('Y-m-d H:i') }} by XephyGuard. Keystrokes and clipboard text are never recorded; screen evidence is saved only on explicit instructor capture.</small></p>
