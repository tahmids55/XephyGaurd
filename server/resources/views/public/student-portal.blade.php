@extends('layouts.app')
@section('title', 'Student Transparency Portal & Appeals')
@section('content')
<div class="mx-auto max-w-5xl space-y-6 text-sm">
  <div class="card flex flex-wrap items-center justify-between gap-4">
    <div>
      <h1 class="text-2xl font-bold">Student Transparency Portal & Appeals</h1>
      <p class="text-slate-600">Inspect every telemetry signal, evidence confidence level, Process & AI Disclosure Work Journal entry, and submit or track appeals.</p>
    </div>
    <a href="{{ url('/notice') }}" class="btn btn-sm">Monitoring Notice</a>
  </div>

  @if (! ($student ?? null))
    <div class="card max-w-xl space-y-3">
      <h2 class="font-semibold text-base">Access Your Transparency Record</h2>
      @if ($invalidToken ?? false)
        <div class="rounded border border-red-300 bg-red-50 p-3 text-red-700">Invalid portal token. Please check the link or token provided by your instructor.</div>
      @endif
      <form method="get" action="{{ url('/student-portal') }}" class="flex gap-2">
        <input name="token" class="input flex-1 font-mono" placeholder="Paste your 48-character portal token" required>
        <button class="btn btn-primary">Open Portal</button>
      </form>
    </div>
  @else
    <div class="card flex flex-wrap items-center justify-between gap-4">
      <div class="space-y-1">
        <div class="text-lg font-bold">{{ $student['name'] }} <span class="font-mono text-sm text-slate-500">({{ $student['student_number'] }} · {{ $student['login'] }})</span></div>
        <div class="text-xs text-slate-600">
          Research Pseudonym: <code class="font-mono">{{ $student['pseudonym'] }}</code>
          @if ($student['exempt'])
            · <span class="badge bg-emerald-100 text-emerald-800">Accommodation Exempt ({{ $student['exempt_reason'] }})</span>
          @endif
          @if ($student['legal_hold'])
            · <span class="badge bg-amber-100 text-amber-800">Legal Hold Active (Protected from Retention Purge)</span>
          @endif
        </div>
      </div>
      <div>
        <a href="{{ url('/student-portal/export?token='.urlencode($token)) }}" class="btn btn-primary">Download My Full Data (JSON)</a>
      </div>
    </div>

    <div class="grid gap-4 lg:grid-cols-2">
      <div class="card space-y-3">
        <h2 class="font-semibold text-base">My Lab Sessions & Integrity Scores</h2>
        @if ($sessions->isEmpty())
          <p class="text-slate-500">No lab sessions recorded yet.</p>
        @else
          <table class="tbl">
            <tr><th>Session</th><th>Mode / AI Policy</th><th>Score</th><th>Hold</th></tr>
            @foreach ($sessions as $s)
              @php($sc = $scores->firstWhere('lab_session_id', $s->id))
              <tr>
                <td><b>{{ $s->course }}</b> (#{{ $s->id }})<div class="text-xs text-slate-500">{{ $s->starts_at?->format('Y-m-d H:i') }}</div></td>
                <td><span class="badge mode-{{ $s->mode }}">{{ $s->mode }}</span> <span class="badge sev-info">{{ $s->ai_policy ?? 'forbidden' }}</span></td>
                <td>
                  @if ($sc)
                    <b>{{ $sc->score }}</b> <span class="text-xs text-slate-500">({{ $sc->confidence }})</span>
                  @else
                    <span class="text-slate-400">—</span>
                  @endif
                </td>
                <td>@if ($s->legal_hold)<span class="badge bg-amber-100 text-amber-800">Legal Hold</span>@else — @endif</td>
              </tr>
            @endforeach
          </table>
        @endif
      </div>

      <div class="card space-y-3">
        <h2 class="font-semibold text-base">Submit an Appeal or Context Statement</h2>
        <p class="text-xs text-slate-600">Submitting an appeal automatically places a legal hold on your session data so nothing is deleted before your instructor completes their review.</p>
        <form method="post" action="{{ url('/student-portal/appeals') }}" class="space-y-3">
          @csrf
          <input type="hidden" name="token" value="{{ $token }}">
          <div>
            <label class="label">Lab Session</label>
            <select name="lab_session_id" class="input" required>
              @foreach ($sessions as $s)
                <option value="{{ $s->id }}">{{ $s->course }} (#{{ $s->id }} — {{ $s->starts_at?->format('Y-m-d H:i') }})</option>
              @endforeach
            </select>
          </div>
          <div>
            <label class="label">Specific Flag / Alert (optional)</label>
            <select name="alert_id" class="input">
              <option value="">— General session appeal —</option>
              @foreach ($alerts as $a)
                <option value="{{ $a->id }}">[#{{ $a->id }}] {{ $a->rule_id }} — {{ $a->title }}</option>
              @endforeach
            </select>
          </div>
          <div>
            <label class="label">Your Explanation & Context</label>
            <textarea name="student_statement" rows="3" class="input" placeholder="Explain what happened (e.g., background extension from an earlier class, starter code paste, permitted disclosure)..." required></textarea>
          </div>
          <button class="btn btn-primary">Submit Appeal</button>
        </form>
      </div>
    </div>

    <div class="card space-y-3">
      <h2 class="font-semibold text-base">My Appeals History</h2>
      @if ($appeals->isEmpty())
        <p class="text-slate-500">No appeals submitted.</p>
      @else
        <table class="tbl">
          <tr><th>ID</th><th>Session</th><th>Flag</th><th>Status</th><th>Your Statement</th><th>Instructor Decision</th></tr>
          @foreach ($appeals as $ap)
            <tr>
              <td>#{{ $ap->id }}</td>
              <td>{{ $ap->session?->course ?? '#'.$ap->lab_session_id }}</td>
              <td>{{ $ap->alert?->rule_id ?? 'Session' }}</td>
              <td><span class="badge sev-info">{{ $ap->status }}</span></td>
              <td>{{ $ap->student_statement }}</td>
              <td>
                @if ($ap->decision_reason)
                  <div><b>{{ $ap->status }}</b>: {{ $ap->decision_reason }}</div>
                  <div class="text-xs text-slate-500">{{ $ap->decider?->name }} · {{ $ap->decided_at?->format('Y-m-d H:i') }}</div>
                @else
                  <span class="text-slate-400">Pending instructor review</span>
                @endif
              </td>
            </tr>
          @endforeach
        </table>
      @endif
    </div>

    <div class="card space-y-3">
      <h2 class="font-semibold text-base">Process & AI Disclosure Work Journal</h2>
      <p class="text-xs text-slate-600">Chronological log of work-folder file saves and disclosed AI tool activity showing authentic, incremental coursework progression.</p>
      @if (empty($work_journal))
        <p class="text-slate-500">No work journal entries recorded yet.</p>
      @else
        <table class="tbl">
          <tr><th>Time</th><th>PC</th><th>Kind</th><th>Summary</th><th>Interpretation</th></tr>
          @foreach ($work_journal as $wj)
            <tr>
              <td class="font-mono text-xs">{{ $wj['occurred_at'] }}</td>
              <td>{{ $wj['pc'] }}</td>
              <td><span class="badge sev-info">{{ $wj['event_class'] }}</span></td>
              <td class="font-medium">{{ $wj['summary'] }}</td>
              <td class="text-xs text-slate-600">{{ $wj['evidence_meta']['what_it_means'] ?? '' }}</td>
            </tr>
          @endforeach
        </table>
      @endif
    </div>

    <div class="card space-y-3">
      <h2 class="font-semibold text-base">Flags & Evidence Explanations</h2>
      @if ($alerts->isEmpty())
        <p class="text-slate-500">No flags or alerts recorded for your login.</p>
      @else
        <table class="tbl">
          <tr><th>Time</th><th>Rule</th><th>Class & Confidence</th><th>What It Means</th><th>What It Does NOT Mean</th></tr>
          @foreach ($alerts as $a)
            @php($meta = $a->evidence_meta)
            <tr>
              <td class="font-mono text-xs">{{ $a->occurred_at?->format('Y-m-d H:i:s') }}</td>
              <td><b>{{ $a->rule_id }}</b><div class="text-xs text-slate-600">{{ $a->title }}</div></td>
              <td>
                <span class="badge sev-info">{{ $meta['event_class'] }}</span>
                <span class="badge sev-{{ $a->severity }}">{{ $meta['confidence'] }}</span>
              </td>
              <td class="text-xs">{{ $meta['what_it_means'] }}</td>
              <td class="text-xs text-slate-600">{{ $meta['what_it_does_not_mean'] }}</td>
            </tr>
          @endforeach
        </table>
      @endif
    </div>

    <div class="card space-y-3">
      <h2 class="font-semibold text-base">Raw Telemetry Events ({{ $events->count() }})</h2>
      <table class="tbl">
        <tr><th>Time</th><th>Type</th><th>Class</th><th>Confidence</th><th>What It Means</th><th>What It Does NOT Mean</th></tr>
        @foreach ($events as $ev)
          @php($em = $ev->evidence_meta)
          <tr>
            <td class="font-mono text-xs">{{ $ev->occurred_at?->format('H:i:s') }}</td>
            <td class="font-mono text-xs">{{ $ev->type }}</td>
            <td><span class="badge sev-info">{{ $em['event_class'] }}</span></td>
            <td>{{ $em['confidence'] }}</td>
            <td class="text-xs">{{ $em['what_it_means'] }}</td>
            <td class="text-xs text-slate-600">{{ $em['what_it_does_not_mean'] }}</td>
          </tr>
        @endforeach
      </table>
    </div>
  @endif
</div>
@endsection
