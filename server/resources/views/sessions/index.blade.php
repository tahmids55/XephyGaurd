@extends('layouts.app', ['activePage' => 'sessions.html'])
@section('title', 'Sessions')
@section('content')
<div class="doc" x-data="sessionsPage"><div class="optbar"><span class="otitle">Sessions</span><span class="osub">Start, watch and review lab sessions.</span><span class="grow"></span><button class="btn btn-solid" @click="startOpen = true"><svg class="i i-sm" aria-hidden="true"><use href="#i-plus"/></svg> Start a session</button></div><div class="canvas ">
  

  @if(!empty($active))
  @php
    $startAt = $active->started_at ?? $active->starts_at ?? now();
    $endAt = $active->ends_at ?? $startAt->copy()->addMinutes(90);
    $totalMin = max(1, (int) $startAt->diffInMinutes($endAt));
    $elapsedMin = max(0, min($totalMin, (int) $startAt->diffInMinutes(now())));
    $leftMin = max(0, (int) now()->diffInMinutes($endAt, false));
    $pct = min(100, max(0, (int) round(($elapsedMin / $totalMin) * 100)));
  @endphp
  <section class="nowhero">
    <div class="row" style="align-items:flex-start;--gap:12px">
      <div class="grow">
        <div class="row" style="--gap:8px;margin-bottom:8px"><span class="mode {{ $active->mode }}">{{ ucfirst(str_replace('_', ' ', $active->mode)) }}</span><span class="chip ok"><span class="dot ok"></span>Running</span></div>
        <div class="title">{{ $active->course }}</div>
        <p class="muted" style="margin-top:4px">{{ $active->profile?->name ?? ucfirst($active->mode) }} · started {{ $startAt->format('H:i') }} · {{ $totalMin }} minutes · {{ $active->sessionPcs?->count() ?? 0 }} PCs</p>
      </div>
      @if(!empty($profiles) && count($profiles))
      <form method="post" action="{{ route('sessions.switch', $active) }}" class="row" style="--gap:6px">
        @csrf
        <select name="profile_id" class="select select-sm" style="width:190px" aria-label="Switch session profile">
          @foreach($profiles as $prof)
            <option value="{{ $prof->id }}" @selected($active->profile_id === $prof->id)>{{ $prof->name }}</option>
          @endforeach
        </select>
        <button type="submit" class="btn btn-sm">Switch profile</button>
      </form>
      @endif
      @if(!empty($proxyOnly))
      <form method="post" action="{{ route('sessions.network_lock', $active) }}" class="row" style="--gap:6px">
        @csrf
        <select name="network_lock" class="select select-sm" style="width:190px" aria-label="Network lock">
          <option value="observe" @selected($active->network_lock === 'observe')>Network: observe</option>
          <option value="strict" @selected($active->network_lock === 'strict')>Network: strict (proxy only)</option>
          <option value="off" @selected($active->network_lock === 'off')>Network: no lock</option>
        </select>
        <button type="submit" class="btn btn-sm">Set network</button>
      </form>
      @endif
      <form method="post" action="{{ route('sessions.stop', $active) }}" @submit="confirmStop($event)">
        @csrf
        <button type="submit" class="btn btn-danger">End session</button>
      </form>
    </div>
    <div class="clock" aria-label="{{ $leftMin }} minutes remaining">
      <span class="tick" style="left:{{ $pct }}%">{{ now()->format('H:i') }}</span>
      <div class="track"><i style="width:{{ $pct }}%"></i></div>
      <div class="ends"><span>{{ $startAt->format('H:i') }}</span><span>{{ $leftMin }} min left</span><span>{{ $endAt->format('H:i') }}</span></div>
    </div>
  </section>
  @else
  <section class="nowhero">
    <div class="row" style="align-items:center;--gap:12px">
      <div class="grow">
        <div class="row" style="--gap:8px;margin-bottom:8px"><span class="mode always">Always-on</span><span class="chip plain">Idle</span></div>
        <div class="title">No active lab session</div>
        <p class="muted" style="margin-top:4px">PCs follow the always-on baseline policy until a timed session starts.</p>
      </div>
      <button class="btn btn-solid" @click="startOpen = true"><svg class="i i-sm" aria-hidden="true"><use href="#i-plus"/></svg> Start a session</button>
    </div>
  </section>
  @endif

  <div class="cols-main">
    <div>
      <section class="panel">
        <div class="panel-head"><h2 class="h2">Per-PC rules</h2><span class="chip plain">Changes are audited</span></div>
        <div class="table-wrap"><table class="tbl">
          <thead><tr><th>PC</th><th>Rules</th><th>Reason</th><th></th></tr></thead>
          <tbody>
            @if(!empty($active) && $active->sessionPcs && $active->sessionPcs->count())
              @foreach($active->sessionPcs as $sp)
                @if($sp->pc)
                <tr>
                  <td class="name">{{ $sp->pc->name }}</td>
                  <td colspan="3" style="padding:0">
                    <form method="post" action="{{ route('sessions.override', [$active, $sp->pc]) }}" class="row" style="padding:10px 16px;--gap:10px;flex-wrap:nowrap">
                      @csrf
                      <select name="profile_id" class="select select-sm" style="width:200px">
                        <option value="">Same as session</option>
                        @foreach($profiles ?? [] as $prof)
                          <option value="{{ $prof->id }}" @selected($sp->override_profile_id === $prof->id)>{{ $prof->name }}</option>
                        @endforeach
                      </select>
                      <input name="reason" class="input input-sm grow" value="{{ $sp->override_reason }}" placeholder="Why does this PC differ?" required>
                      <button type="submit" class="btn btn-sm">Save</button>
                    </form>
                  </td>
                </tr>
                @endif
              @endforeach
            @else
              <tr><td colspan="4" class="muted" style="padding:18px 16px">{{ !empty($active) ? 'No per-PC rule overrides configured for this session.' : 'Start a session to configure per-PC rule overrides.' }}</td></tr>
            @endif
          </tbody></table></div>
      </section>
      <section class="panel">
        <div class="panel-head"><h2 class="h2">Past sessions</h2><a class="small" href="{{ route('reports.index') }}">All reports</a></div>
        <div class="table-wrap"><table class="tbl">
          <thead><tr><th>When</th><th>Course</th><th>Profile</th><th></th></tr></thead>
          <tbody>
            @if(!empty($history) && $history->count())
              @foreach($history as $s)
                <tr>
                  <td class="nw">{{ $s->starts_at?->format('M d · H:i') }} – {{ $s->ended_at ? $s->ended_at->format('H:i') : 'now' }}</td>
                  <td class="name">{{ $s->course }}</td>
                  <td><span class="mode {{ $s->mode }}">{{ ucfirst(str_replace('_', ' ', $s->mode)) }}</span></td>
                  <td class="right">
                    @if($s->status === 'active')
                      <span class="chip ok">Running</span>
                    @else
                      <a href="{{ route('reports.show', $s) }}">View report</a>
                    @endif
                  </td>
                </tr>
              @endforeach
            @else
              <tr><td colspan="4" class="muted" style="padding:18px 16px">No sessions recorded yet.</td></tr>
            @endif
          </tbody></table></div>
        <div class="tbl-foot">
          {{ !empty($history) ? $history->count() . ' of ' . $history->total() . ' sessions' : '0 sessions' }}
          @if(!empty($history) && $history->hasPages())
            <div class="pager">
              @if($history->onFirstPage())
                <button disabled>Prev</button>
              @else
                <a class="btn btn-sm" href="{{ $history->previousPageUrl() }}">Prev</a>
              @endif
              @if($history->hasMorePages())
                <a class="btn btn-sm" href="{{ $history->nextPageUrl() }}">Next</a>
              @else
                <button disabled>Next</button>
              @endif
            </div>
          @endif
        </div>
      </section>
    </div>
    <div>
      <form method="post" action="{{ route('sessions.message') }}" class="panel panel-pad stack">
        @csrf
        <div><h2 class="h2">Broadcast a message</h2><p class="muted small" style="margin-top:2px">Appears on every connected student screen.</p></div>
        <textarea name="text" class="textarea" rows="3" placeholder="e.g. 10 minutes left. Please commit your code." x-model="bc" required></textarea>
        <div class="pills"><button type="button" class="pill" @click="bc = '10 minutes left. Please commit your code.'">10 minute warning</button><button type="button" class="pill" @click="bc = 'Please close other browser tabs.'">Close tabs</button></div>
        <button type="submit" class="btn btn-solid"><svg class="i i-sm" aria-hidden="true"><use href="#i-send"/></svg> Send to all PCs</button>
      </form>
      <section class="panel">
        <div class="panel-head"><h2 class="h2">Recent commands</h2></div>
        <ul class="cmdlog" style="padding:4px 20px 8px">
          @if(!empty($commands) && $commands->count())
            @foreach($commands as $c)
              <li><span class="mono muted small">{{ $c->created_at?->format('H:i') }}</span><span><b>{{ $c->pc?->name ?? 'All PCs' }}</b> {{ str_replace('_', ' ', $c->kind) }}</span><span class="chip {{ $c->status === 'done' || $c->status === 'acked' ? 'ok' : 'plain' }}">{{ ucfirst($c->status ?? 'Sent') }}</span></li>
            @endforeach
          @else
            <li class="muted small" style="padding:12px 0">No commands dispatched yet.</li>
          @endif
        </ul>
      </section>
    </div>
  </div>

  <div class="overlay" x-show="startOpen" x-cloak x-transition.opacity @click.self="startOpen = false" @keydown.escape.window="startOpen = false">
    <div class="modal" style="width:min(520px,100%)" role="dialog" aria-label="Start a session">
      <div class="modal-head"><span class="h2">Start a session</span><button class="xbtn" @click="startOpen = false" aria-label="Close"><svg class="i" aria-hidden="true"><use href="#i-x"/></svg></button></div>
      <form method="post" action="{{ route('sessions.start') }}">
        @csrf
        <div class="modal-body stack">
          <label class="field"><span>Course</span><input name="course" class="input" placeholder="e.g. CSE 3100" required></label>
          <div class="form-grid">
            <label class="field"><span>Policy profile</span>
              <select name="profile_id" class="select" required>
                @foreach($profiles ?? [] as $prof)
                  <option value="{{ $prof->id }}">{{ $prof->name }}</option>
                @endforeach
              </select>
            </label>
            <label class="field"><span>Duration (minutes)</span><input name="duration_min" class="input" type="number" min="10" value="90" placeholder="90"></label>
          </div>
          <div class="form-grid">
            <label class="field"><span>Research condition</span>
              <select name="experiment_condition" class="select">
                <option value="">None</option>
                <option value="baseline">Baseline</option>
                <option value="treatment">Treatment</option>
              </select>
            </label>
            <label class="field"><span>Cohort / group (optional)</span><input name="experiment_group" class="input" placeholder="e.g. Group A"></label>
          </div>
          @if(!empty($proxyOnly))
          <label class="field"><span>Network lock</span>
            <select name="network_lock" class="select">
              <option value="observe" selected>Observe: everything allowed, QUIC off so browsers use the proxy; tunnels are flagged</option>
              <option value="strict">Strict: Internet only through the lab proxy</option>
              <option value="off">No lock</option>
            </select>
          </label>
          @endif
        </div>
        <div class="modal-foot"><button type="button" class="btn" @click="startOpen = false">Cancel</button><button type="submit" class="btn btn-solid">Start session</button></div>
      </form>
    </div>
  </div>
</div></div>
@endsection
