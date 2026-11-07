{{-- Shared by the HTML report and the PDF export. --}}
<h1 style="margin:0">Integrity report — {{ $session->course }}{{ isset($student) ? ' — '.$student->name : '' }}</h1>
<p>{{ $session->starts_at->format('Y-m-d H:i') }}–{{ $session->ended_at?->format('H:i') }} · profile “{{ $session->profile?->name }}” · mode {{ $session->mode }}
  @if ($session->experiment_condition) · condition {{ $session->experiment_condition }} @endif</p>
<p style="padding:8px;border:1px solid #f59e0b;background:#fffbeb"><b>Flag for human review.</b> Scores summarise observable signals; they are not proof of misconduct.
  Students marked exempt (accommodations) and assignment-provided boilerplate can raise signals legitimately.</p>
<p><b>Evidence Summary:</b> Installed (Inventory): <b>{{ $installed_count ?? 0 }}</b> · Active Use: <b>{{ $active_count ?? 0 }}</b> · Blocked Attempts: <b>{{ $blocked_count ?? 0 }}</b></p>

<h2>Scores</h2>
<table class="tbl" width="100%" cellspacing="0" cellpadding="4" border="1" style="border-collapse:collapse">
  <tr><th>PC</th><th>Student</th><th>Method</th><th>Score</th><th>Confidence</th><th>Main reasons</th><th>Not observable</th></tr>
  @forelse ($scores as $s)
    <tr><td>{{ $s->pc?->name }}</td><td>{{ $s->student?->name ?? $s->user }}</td><td>{{ $s->method }}</td><td><b>{{ $s->score }}</b>/100</td><td>{{ $s->confidence }}</td>
      <td>@foreach (collect($s->reasons)->take(5) as $r)<div>+{{ $r['points'] ?? '' }} {{ $r['label'] ?? '' }} <small>({{ $r['signal'] ?? '' }})</small></div>@endforeach</td>
      <td><small>{{ implode('; ', $s->not_observable ?? []) }}</small></td></tr>
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
<p><small>Generated {{ now()->format('Y-m-d H:i') }} by XephyGuard. Clipboard text, keystrokes and screen content are never recorded.</small></p>
