@extends('layouts.app', ['activePage' => 'reports.html'])
@section('title', 'Session report')
@section('content')
@if(!empty($session))
<div class="doc" x-data="sessionReportPage"><div class="optbar noprint">
  @if(!empty($student))
    <a class="btn btn-quiet btn-sm" href="{{ route('reports.show', $session) }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-back"/></svg> Session report</a>
  @else
    <a class="btn btn-quiet btn-sm" href="{{ route('reports.index') }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-back"/></svg> Reports</a>
  @endif
  <span class="otitle">{{ !empty($student) ? 'Student report · ' . $student->name : 'Session report' }}</span>
  <span class="osub">{{ $session->course }} · {{ ($session->starts_at ?? now())->format('j F Y') }}</span>
  <span class="grow"></span>
  <form method="post" action="{{ route('reports.rescore', $session) }}" style="display:inline">
    @csrf
    <button type="submit" class="btn btn-sm"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Re-score</button>
  </form>
  <a class="btn btn-sm" href="{{ route('reports.csv', $session) }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> CSV</a>
  <a class="btn btn-sm" href="{{ route('reports.pdf', $session) }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> PDF</a>
  <button type="button" class="btn btn-sm btn-solid" @click="printReport()"><svg class="i i-sm" aria-hidden="true"><use href="#i-print"/></svg> Print or save as PDF</button>
</div>
<div class="canvas pad"><article class="paper">
  <div class="row" style="--gap:10px;margin-bottom:12px"><span class="mode {{ $session->mode }}">{{ ucfirst(str_replace('_', ' ', $session->mode)) }}</span><span class="muted small">Academic integrity report</span></div>
  <h1>{{ $session->course }}{{ !empty($student) ? ' · ' . $student->name . ' (' . $student->student_number . ')' : '' }}</h1>
  <p class="meta">{{ ($session->starts_at ?? now())->format('j F Y · H:i') }}{{ $session->ended_at ? ' – ' . $session->ended_at->format('H:i') : '' }} · Profile “{{ $session->profile?->name ?? ucfirst($session->mode) }}” · {{ isset($scores) ? $scores->count() : 0 }} scores</p>
  <p class="disclaimer"><b>Read this first.</b> Scores summarise signals the system could observe. They are not proof of anything, and some things cannot be observed at all. A flag is a reason to talk to the student.</p>
  <h2>1. Scores</h2>
  <table class="tbl"><thead><tr><th>PC</th><th>Student</th><th>Method</th><th>Score</th><th>Confidence</th><th>Reasons</th><th>Not observable</th></tr></thead><tbody>
    @forelse(\App\Services\ReportModel::scoreRows($scores ?? collect()) as $i => $r)
      @php
        $s = $scores[$i];
        $barClass = $r['score'] >= 70 ? 'low' : ($r['score'] >= 40 ? 'mid' : '');
      @endphp
      <tr>
        <td class="name">{{ $r['pc'] }}</td>
        <td class="mono">
          @if($s->student)
            <a href="{{ route('reports.student', [$session, $s->student]) }}">{{ $s->student->name }} ({{ $r['user'] ?: $s->student->login }})</a>
          @else
            {{ $r['user'] ?: '—' }}
          @endif
        </td>
        <td class="muted small">{{ $r['method'] }}</td>
        <td><div class="scorebar {{ $barClass }}"><i><b style="width:{{ min(100, max(0, $r['score'])) }}%"></b></i><span>{{ $r['score'] }}/100</span></div></td>
        <td>{{ ucfirst($r['confidence'] ?: 'medium') }}</td>
        <td>
          @if(count($r['reasons']))
            <div class="signals">
              @foreach($r['reasons'] as $x)
                <span>@if($x['points'] !== null)<b>+{{ $x['points'] }}</b>@endif{{ $x['label'] }}@if($x['signal'] !== '') <small class="muted">({{ $x['signal'] }})</small>@endif</span>
              @endforeach
            </div>
          @else
            <span class="muted">No reasons recorded</span>
          @endif
        </td>
        <td class="small muted">{{ implode('; ', $r['not_observable']) ?: '—' }}</td>
      </tr>
    @empty
      <tr><td colspan="7" class="muted">No scores recorded yet. Use Re-score to compute session scores.</td></tr>
    @endforelse
  </tbody></table>
  <p class="small muted" style="margin-top:8px">Highest score first. A higher score means more signals to review, not proof. The CSV has every PC.</p>
  <h2>2. Alerts ({{ isset($alerts) ? $alerts->count() : 0 }})</h2>
  <table class="tbl"><thead><tr><th>Time</th><th>PC</th><th>Severity</th><th>What happened</th><th>Review</th></tr></thead><tbody>
    @forelse($alerts ?? [] as $a)
      <tr>
        <td class="mono">{{ $a->occurred_at?->format('H:i') }}</td>
        <td class="name">{{ $a->pc?->name ?? ('PC-' . $a->pc_id) }}</td>
        <td><span class="sev {{ $a->severity }}">{{ $a->severity }}</span></td>
        <td>{{ $a->title ?: $a->rule_id }}</td>
        <td>{{ $a->acknowledged_at ? 'Reviewed' : 'Open' }}</td>
      </tr>
    @empty
      <tr><td colspan="5" class="muted">No alerts raised during this session.</td></tr>
    @endforelse
  </tbody></table>
  @if(!empty($blocked) && $blocked->count())
  <h2>3. Blocked connections ({{ $blocked->count() }})</h2>
  <table class="tbl"><thead><tr><th>PC</th><th>Host</th><th>Category</th><th class="right">Attempts</th><th>First seen</th></tr></thead><tbody>
    @foreach($blocked as $b)
      <tr>
        <td class="name">{{ $b->pc?->name ?? ('PC-' . $b->pc_id) }}</td>
        <td class="mono">{{ $b->host }}</td>
        <td>{{ $b->category }}</td>
        <td class="right num">{{ $b->n }}</td>
        <td class="mono muted">{{ \Illuminate\Support\Carbon::parse($b->first_at)->format('H:i:s') }}</td>
      </tr>
    @endforeach
  </tbody></table>
  @endif
  @if(!empty($pastes) && $pastes->count())
  <h2>Large pastes ({{ $pastes->count() }})</h2>
  <table class="tbl"><thead><tr><th>Time</th><th>Size</th><th>From</th><th>Into</th></tr></thead><tbody>
    @foreach($pastes as $p)
      <tr>
        <td class="mono">{{ $p->occurred_at?->format('H:i:s') }}</td>
        <td>{{ $p->chars }} characters, {{ $p->lines }} lines</td>
        <td>{{ $p->source_app ?: '—' }}</td>
        <td>{{ $p->target_app ?: '—' }}</td>
      </tr>
    @endforeach
  </tbody></table>
  @endif
  @if(!empty($evidence) && $evidence->count())
  <h2>Saved screen evidence ({{ $evidence->count() }})</h2>
  <table class="tbl"><thead><tr><th>Time</th><th>PC</th><th>Student</th><th>Mode</th><th>Captured by</th><th>SHA-256</th><th></th></tr></thead><tbody>
    @foreach($evidence as $e)
      <tr>
        <td class="mono">{{ $e->occurred_at?->format('H:i:s') }}</td>
        <td class="name">{{ $e->pc?->name ?? ('PC-' . $e->pc_id) }}</td>
        <td class="mono">{{ $e->user ?: '—' }}</td>
        <td>{{ $e->source_mode }} · mon {{ $e->monitor + 1 }}</td>
        <td>{{ $e->capturer?->name ?? '—' }}</td>
        <td class="mono small muted">{{ substr($e->sha256, 0, 12) }}</td>
        <td class="right"><a class="btn btn-sm" href="{{ route('evidence.show', $e) }}" target="_blank" rel="noopener">View</a></td>
      </tr>
    @endforeach
  </tbody></table>
  @endif
  <h2>What could not be observed</h2>
  <p style="color:#46505c">Phones and other devices, notes on paper, and anything said between students. Screen snapshots are taken only when an instructor asks for one.</p>
  <p class="paper-foot">Generated by XephyGuard. Keystrokes, clipboard text and passwords are never recorded.</p>
</article></div></div>
@else
<div class="doc">
  <div class="optbar noprint"><a class="btn btn-quiet btn-sm" href="{{ route('reports.index') }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-back"/></svg> Reports</a><span class="otitle">Session report</span></div>
  <div class="canvas pad"><div class="panel empty-state"><b>Session not found</b>Select a completed session from Reports.</div></div>
</div>
@endif
@endsection
