@extends('layouts.app', ['activePage' => 'index.html'])
@section('title', 'Live lab')
@section('content')
@php
  $seatById = isset($seats) ? collect($seats)->keyBy('id') : collect();
  $livePcs = isset($pcs) && count($pcs) ? collect($pcs)->values()->map(function ($p, $idx) use ($seatById) {
      $seat = !empty($p['seat_id']) ? $seatById->get($p['seat_id']) : null;
      $caps = is_array($p['capabilities'] ?? null) ? $p['capabilities'] : [];
      $scr = is_array($caps['screen'] ?? null) ? $caps['screen'] : [];
      $status = $p['status'] ?? 'normal';
      $score = (int) ($p['score'] ?? 0);
      $user = $p['user'] ?? '';
      $uiStatus = \App\Models\Pc::resolveUiStatus($status, $score, $user);
      $scenes = ['code', 'terminal', 'split', 'writer', 'calc', 'browser'];
      return [
          'id' => $p['id'],
          'name' => $p['name'],
          'host' => $p['hostname'] ?? $p['name'],
          'ip' => $p['ip'] ?? '—',
          'os' => trim(($p['os'] ?? 'Linux') . ' ' . ($p['os_version'] ?? '')),
          'session' => ($scr['session_type'] ?? 'x11') === 'wayland' ? 'wayland' : (($p['os'] ?? '') === 'windows' ? 'win' : 'x11'),
          'agent' => $p['agent_version'] ?? '—',
          'user' => $user !== '' && $user !== null ? $user : 'No one signed in',
          'app' => $p['app'] ?: '—',
          'last' => $p['last_event'] ?: 'Active',
          'score' => $uiStatus === 'offline' || $uiStatus === 'idle' ? '—' : $score,
          'status' => $uiStatus,
          'isolated' => (bool) ($p['isolated'] ?? false),
          'monitors' => (int) ($scr['monitors'] ?? 1) ?: 1,
          'seat' => $seat ? ['x' => (int) $seat->x, 'y' => (int) $seat->y] : null,
          'scene' => $uiStatus === 'offline' ? 'off' : ($uiStatus === 'idle' ? 'idle' : ($uiStatus === 'alert' ? 'flag' : $scenes[$idx % count($scenes)])),
          'caps' => [
              'snapshot' => (bool) ($scr['snapshot'] ?? ($uiStatus !== 'offline')),
              'live' => (bool) ($scr['live'] ?? ($uiStatus !== 'offline')),
              'delta' => (bool) ($scr['delta_tiles'] ?? ($uiStatus !== 'offline')),
              'multi' => ((int) ($scr['monitors'] ?? 1)) > 1,
              'secure' => (bool) ($scr['secure_desktop'] ?? (($p['os'] ?? '') === 'windows')),
          ],
      ];
  })->all() : [];

  $liveAlerts = isset($recentAlerts) && count($recentAlerts) ? collect($recentAlerts)->map(function ($a) {
      return [
          'id' => $a['id'],
          'pcId' => $a['pc_id'],
          'pc' => $a['pc'] ?? ('PC-' . $a['pc_id']),
          'pcRetired' => (bool) ($a['pc_retired'] ?? false),
          'user' => $a['user'] ?: '—',
          'sev' => $a['severity'] ?? 'medium',
          'title' => $a['title'] ?? $a['rule_id'],
          'rule' => $a['rule_id'] ?? '',
          'at' => $a['occurred_at'] ?? '',
          'ago' => (int) ($a['ago_min'] ?? 0),
          'acked' => (bool) ($a['acked'] ?? false),
          'by' => $a['by'] ?? '',
          'note' => $a['note'] ?? '',
      ];
  })->all() : [];
@endphp
<script type="application/json" id="xg-live-data">{!! json_encode([
  'cols' => (int) ($lab->grid_cols ?? 6),
  'rows' => (int) ($lab->grid_rows ?? 4),
  'lab' => ['cols' => (int) ($lab->grid_cols ?? 6), 'rows' => (int) ($lab->grid_rows ?? 4)],
  'pcs' => $livePcs,
  'alerts' => $liveAlerts,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="labView" @keydown.escape.window="clear()">

  <div class="optbar">
    <span class="otitle">Live lab</span>
    @if(!empty($session))
      <span class="mode {{ $session->mode }}">{{ str_replace('_', ' ', ucfirst($session->mode)) }}</span>
      <span class="osub">{{ $session->course }}{{ $session->ends_at ? ' · ' . max(0, (int) now()->diffInMinutes($session->ends_at, false)) . ' min left' : '' }}</span>
    @else
      <span class="mode always">Always-on</span><span class="osub">No active session</span>
      <a class="btn btn-sm btn-quiet" href="{{ route('sessions.index') }}#start">Start session</a>
    @endif
    <div class="seg" style="margin-left:12px" role="group" aria-label="View">
      <button :aria-pressed="view === 'map'" @click="view = 'map'"><svg class="i i-sm" aria-hidden="true"><use href="#i-live"/></svg> Map</button>
      <button :aria-pressed="view === 'wall'" @click="view = 'wall'"><svg class="i i-sm" aria-hidden="true"><use href="#i-grid"/></svg> Wall</button>
    </div>
    <span class="grow"></span>
    <template x-if="view === 'wall'">
      <div class="row" style="--gap:10px">
        <div class="seg" role="group" aria-label="Tile size">
          <button :aria-pressed="tile === 180" @click="tileSize(180)">Small</button>
          <button :aria-pressed="tile === 240" @click="tileSize(240)">Medium</button>
          <button :aria-pressed="tile === 340" @click="tileSize(340)">Large</button>
        </div>
        <button class="pill" :aria-pressed="flagged" @click="flagged = !flagged">Flagged only</button>
        <label class="check small"><span class="switch"><input type="checkbox" x-model="blur"><i></i></span> Blur screens</label>
      </div>
    </template>
    <button class="btn" x-show="view === 'map'" :class="editing && 'btn-solid'" @click="editing ? saveLayout() : editing = true" x-text="editing ? 'Done editing' : 'Edit seat map'"></button>
    <button class="btn" @click="message(null)"><svg class="i i-sm" aria-hidden="true"><use href="#i-send"/></svg> Message all</button>
    @if(!empty($session))
      @if(!empty($profiles) && count($profiles))
      <form method="post" action="{{ route('sessions.switch', $session) }}" style="display:inline-flex;gap:6px;align-items:center">
        @csrf
        <select name="profile_id" class="select select-sm" style="width:170px" aria-label="Switch profile" @change="$el.form?.submit()">
          @foreach($profiles as $prof)
            <option value="{{ $prof->id }}" @selected($session->profile_id === $prof->id)>{{ $prof->name }}</option>
          @endforeach
        </select>
      </form>
      @endif
      <form method="post" action="{{ route('sessions.stop', $session) }}" style="display:inline" @submit="confirmForm($event, 'End the session? Restrictions lift immediately.')">
        @csrf
        <button type="submit" class="btn btn-danger">End session</button>
      </form>
    @else
      <a class="btn btn-solid" href="{{ route('sessions.index') }}#start">Start a session</a>
    @endif
  </div>

  <div class="work" :class="view === 'wall' && 'full'">

    <!-- MAP -->
    <section class="center" x-show="view === 'map'" aria-label="Seat map">
      <div class="pulse">
        <div class="tally" role="img" aria-label="Seat status">
          <i class="t-ok" x-show="counts.ok" :style="'flex:'+counts.ok"></i><i class="t-idle" x-show="counts.idle" :style="'flex:'+counts.idle"></i>
          <i class="t-watch" x-show="counts.watch" :style="'flex:'+counts.watch"></i><i class="t-alert" x-show="counts.alert" :style="'flex:'+counts.alert"></i>
          <i class="t-off" x-show="counts.off" :style="'flex:'+counts.off"></i>
        </div>
        <div class="legend">
          <span><i class="sw ok"></i><b x-text="counts.ok"></b> normal</span>
          <span><i class="sw watch"></i><b x-text="counts.watch"></b> watch</span>
          <span><i class="sw alert"></i><b x-text="counts.alert"></b> alert</span>
          <span><i class="sw off"></i><b x-text="counts.off"></b> offline</span>
          <span><i class="sw idle"></i><b x-text="counts.idle"></b> not in use</span>
        </div>
      </div>
      <div class="room">
        <div class="note" x-show="editing" x-cloak style="margin:0 auto 14px;max-width:1100px;align-items:center;flex-wrap:wrap">
          <svg class="i i-sm" aria-hidden="true"><use href="#i-info"/></svg><span>Drag a PC onto another seat to move or swap it. Grid:</span>
          <input type="number" min="2" max="12" x-model.number="cols" class="input input-sm" style="width:60px" aria-label="Columns"><span>×</span>
          <input type="number" min="1" max="10" x-model.number="rows" class="input input-sm" style="width:60px" aria-label="Rows">
        </div>
        <div class="front">Instructor desk</div>
        <div class="seats" :style="gridStyle">
          <template x-for="c in cells" :key="c.key">
            <div class="seat" :class="[seatClass(c.pc), dropKey === c.key ? 'drop' : '', c.pc && activePcId === c.pc.id ? 'sel' : '']" :style="'grid-column:'+c.col"
                 :draggable="editing && !!c.pc" @dragstart="startDrag(c.pc)" @dragover.prevent="dropKey = editing ? c.key : null" @dragleave="dropKey = null" @drop.prevent="dropOn(c)"
                 :tabindex="c.pc ? 0 : -1" :role="c.pc ? 'button' : null" @click="c.pc && select(c.pc.id)" @dblclick="c.pc && openInspector(c.pc.id)" @keydown.enter="c.pc && openInspector(c.pc.id)">
              <template x-if="c.pc">
                <div style="display:contents">
                  <div class="top"><span class="pcn"><span class="dot"></span><span x-text="c.pc.name"></span><svg class="i i-sm" x-show="c.pc.isolated" style="color:var(--iso)"><use href="#i-lock"/></svg></span><span class="sc" :class="scoreClass(c.pc)" x-text="c.pc.score"></span></div>
                  <div class="who" x-text="c.pc.user"></div>
                  <div class="sapp" x-text="c.pc.app"></div>
                  <div class="evt" x-show="!['ok','idle'].includes(kind(c.pc))" x-text="c.pc.last"></div>
                </div>
              </template>
              <template x-if="!c.pc"><span x-text="String.fromCharCode(65 + c.y) + (c.x + 1)"></span></template>
            </div>
          </template>
        </div>
        <div class="shelf" x-show="unplaced.length || editing" @dragover.prevent @drop.prevent="dropShelf()">
          <div class="label">Not on the map</div>
          <div class="row">
            <template x-for="p in unplaced" :key="p.id"><div class="seat" :class="seatClass(p)" style="min-height:0;width:180px" :draggable="editing" @dragstart="startDrag(p)" @click="select(p.id)" tabindex="0" role="button"><div class="top"><span class="pcn"><span class="dot"></span><span x-text="p.name"></span></span></div><div class="who" x-text="p.user"></div></div></template>
            <span class="muted small" x-show="!unplaced.length">Drop a PC here to take it off the map.</span>
          </div>
        </div>
      </div>
    </section>

    <!-- WALL -->
    <section class="center" x-show="view === 'wall'" x-cloak aria-label="Screen wall">
      <div class="wall" :style="'--tile:' + tile + 'px'">
        <div class="note" style="margin-bottom:18px"><svg class="i i-sm" aria-hidden="true"><use href="#i-eye"/></svg><span>Students see an “instructor is viewing” indicator while their screen is shared. Thumbnails update about once a second and nothing is recorded. Double-click a screen to inspect it.</span></div>

        <template x-if="pinnedPcs.length">
          <div style="margin-bottom:6px">
            <h3 class="wall-title"><svg class="i i-sm" aria-hidden="true"><use href="#i-pin"/></svg> Pinned <span class="count" x-text="pinnedPcs.length"></span></h3>
            <div class="wall-grid pins">
              <template x-for="p in pinnedPcs" :key="'p' + p.id">
                <div class="tile" :class="[tileClass(p), activePcId === p.id && 'sel', blur && !['alert','watch'].includes(kind(p)) ? 'blurred' : '']" @click="select(p.id)" @dblclick="openInspector(p.id)">
                  <div class="ov"><span class="chip" x-show="!['ok','idle'].includes(kind(p))" x-text="p.last"></span></div>
                  <div class="acts"><button @click.stop="togglePin(p.id)" title="Unpin"><svg class="i i-sm" aria-hidden="true"><use href="#i-pin"/></svg></button><button @click.stop="openInspector(p.id)" title="Inspect"><svg class="i i-sm" aria-hidden="true"><use href="#i-expand"/></svg></button></div>
                  <canvas width="640" height="360" :data-pc="p.id" x-init="xgDraw($el, pc(p.id), performance.now())"></canvas>
                  <div class="offmsg" x-show="p.status === 'offline'">No signal</div>
                  <div class="cap"><span class="dot" :class="dotOf(p)"></span><b x-text="p.name"></b><span class="u" x-text="p.user"></span><span class="sc" x-text="p.score"></span></div>
                </div>
              </template>
            </div>
          </div>
        </template>

        <h3 class="wall-title">All screens <span class="count" x-text="wallPcs.length"></span></h3>
        <div class="wall-grid">
          <template x-for="p in wallPcs" :key="p.id">
            <div class="tile" :class="[tileClass(p), activePcId === p.id && 'sel', blur && !['alert','watch'].includes(kind(p)) ? 'blurred' : '']" @click="select(p.id)" @dblclick="openInspector(p.id)">
              <div class="ov"><span class="chip" x-show="!['ok','idle'].includes(kind(p))" x-text="p.last"></span></div>
              <div class="acts"><button @click.stop="togglePin(p.id)" title="Pin"><svg class="i i-sm" aria-hidden="true"><use href="#i-pin"/></svg></button><button @click.stop="openInspector(p.id)" title="Inspect"><svg class="i i-sm" aria-hidden="true"><use href="#i-expand"/></svg></button></div>
              <canvas width="320" height="180" :data-pc="p.id" x-init="xgDraw($el, pc(p.id), performance.now())"></canvas>
              <div class="offmsg" x-show="p.status === 'offline'">No signal</div>
              <div class="offmsg" x-show="p.session === 'wayland'" style="padding:0 16px;text-align:center;font-size:12px">Screen view isn’t available on Wayland</div>
              <div class="cap"><span class="dot" :class="dotOf(p)"></span><b x-text="p.name"></b><span class="u" x-text="p.user"></span><span class="sc" x-text="p.score"></span></div>
            </div>
          </template>
        </div>
        <div class="empty-state" x-show="!wallPcs.length && !pinnedPcs.length"><b>Nothing flagged</b>Every screen looks normal right now.</div>
      </div>
    </section>

    <!-- DOCK -->
    <aside class="dock" x-show="view === 'map'" aria-label="Details">
      <section class="dsec">
        <div class="dsec-head" role="button" tabindex="0" :aria-expanded="dock.att" @click="dock.att = !dock.att"><svg class="i i-sm" aria-hidden="true"><use href="#i-chev"/></svg> Needs attention <span class="count" x-show="groups.length" x-text="groups.length"></span><span class="sp"></span><a href="{{ route('alerts.index') }}" @click.stop class="small" style="font-weight:500">All alerts</a></div>
        <div x-show="dock.att" style="max-height:300px;overflow:auto">
          <template x-for="g in groups" :key="g.key">
            <div class="q-item" :class="activePcId === g.pcId && 'sel'">
              <div class="row" style="justify-content:space-between"><span class="sev" :class="g.sev" x-text="g.sev"></span><span class="small muted" x-text="xgAgo(g.ago)"></span></div>
              <div class="q-title"><span x-text="g.title"></span> <span class="count" x-show="g.count > 1" x-text="'×' + g.count"></span></div>
              <div class="q-meta"><b x-text="g.pc"></b><span class="chip plain" x-show="g.pcRetired" style="margin-left:6px">retired</span> · <span x-text="g.user"></span></div>
              <div class="q-actions"><button class="btn btn-sm" @click="openInspector(g.pcId, 'alerts')">Open</button><button class="btn btn-sm btn-quiet" @click="ackGroup(g)">Mark reviewed</button></div>
            </div>
          </template>
          <div class="empty-state" x-show="!groups.length"><b>All clear</b>Nothing is waiting for review.</div>
          <div style="padding:10px 16px;border-top:1px solid var(--border)" x-show="doneGroups.length"><button class="link-btn small" @click="showDone = !showDone" x-text="(showDone ? 'Hide' : 'Show') + ' reviewed (' + doneGroups.length + ')'"></button></div>
          <template x-if="showDone"><div><template x-for="g in doneGroups" :key="g.key"><div class="q-item" style="opacity:.65"><div class="q-title" style="margin:0"><span x-text="g.pc + ': ' + g.title"></span></div><div class="q-meta" x-text="'Reviewed by ' + g.by"></div></div></template></div></template>
        </div>
      </section>

      <section class="dsec">
        <div class="dsec-head" role="button" tabindex="0" :aria-expanded="dock.props" @click="dock.props = !dock.props"><svg class="i i-sm" aria-hidden="true"><use href="#i-chev"/></svg> Selected PC <span class="sp"></span></div>
        <div x-show="dock.props">
          <div class="empty-state" x-show="!activePcId" style="padding:24px 16px">Select a seat to see its details. Double-click to inspect.</div>
          <div x-show="activePcId" x-cloak>
            <div style="padding:0 16px 12px">
              <div class="screenbox" style="cursor:pointer" @click="openInspector(activePcId)"><canvas width="320" height="180" :data-pc="activePcId" x-effect="xgDraw($el, pc(activePcId), performance.now())"></canvas></div>
            </div>
            <div class="row" style="padding:0 16px;--gap:10px;flex-wrap:nowrap">
              <div class="scorering" style="width:40px;height:40px" :style="'--v:'+(typeof active.score === 'number' ? active.score : 0)+';--c:'+ringColor(active)"><b x-text="active.score" style="font-size:12px"></b></div>
              <div style="min-width:0"><div style="font-weight:600;font-size:14px" x-text="active.name"></div><div class="small muted" x-text="statusText(active) + ' · ' + active.user"></div></div>
            </div>
            <dl class="prop"><dt>Computer</dt><dd class="mono" x-text="active.host"></dd><dt>Address</dt><dd class="mono" x-text="active.ip"></dd><dt>System</dt><dd x-text="active.os"></dd><dt>Agent</dt><dd x-text="'v' + active.agent"></dd><dt x-show="active.streamCost">Streaming</dt><dd class="mono" x-show="active.streamCost" x-text="active.streamCost"></dd><dt>Last event</dt><dd x-text="active.last"></dd></dl>
            <div class="row" style="padding:0 16px 16px">
              <button class="btn btn-sm btn-solid" @click="openInspector(activePcId)">Open inspector</button>
              <button class="btn btn-sm" @click="message(activePcId)">Message</button>
              <button class="btn btn-sm" :class="active.isolated ? '' : 'btn-danger'" @click="toggleIsolate(active)" x-text="active.isolated ? 'Reconnect' : 'Cut off'"></button>
            </div>
          </div>
        </div>
      </section>

      <section class="dsec" style="border-bottom:0">
        <div class="dsec-head" role="button" tabindex="0" :aria-expanded="dock.list" @click="dock.list = !dock.list"><svg class="i i-sm" aria-hidden="true"><use href="#i-chev"/></svg> All PCs <span class="sp"></span>
          <span class="pills" @click.stop><button class="pill" :aria-pressed="pcFilter === 'all'" @click="pcFilter = 'all'">All</button><button class="pill" :aria-pressed="pcFilter === 'attn'" @click="pcFilter = 'attn'">Flagged</button></span></div>
        <ul class="pclist" x-show="dock.list">
          <template x-for="p in listed" :key="p.id"><li :class="activePcId === p.id && 'sel'" @click="select(p.id)" @dblclick="openInspector(p.id)"><span class="dot" :class="dotOf(p)"></span><span style="font-weight:500" x-text="p.name"></span><span class="u" x-text="p.user"></span><span class="mono small" x-text="p.score"></span></li></template>
        </ul>
      </section>
    </aside>
  </div>

  <!-- INSPECTOR -->
  <div class="sheetscrim" x-show="inspector" x-cloak x-transition.opacity @click="closeInspector()"></div>
  <aside class="inspector" x-show="inspector" x-cloak x-transition:enter="transition ease-out duration-150" x-transition:enter-start="opacity-0" role="dialog" aria-label="PC inspector">
    <div class="insp-head">
      <div class="scorering" :style="'--v:'+(typeof active.score === 'number' ? active.score : 0)+';--c:'+ringColor(active)"><b x-text="active.score"></b></div>
      <div style="min-width:0">
        <div class="row" style="--gap:8px"><span class="h1" x-text="active.name"></span>
          <span class="chip iso" x-show="active.isolated"><svg class="i i-sm" aria-hidden="true"><use href="#i-lock"/></svg> Isolated</span>
          <span class="chip ok" x-show="!active.isolated && active.status !== 'offline'">Online</span>
          <span class="chip" x-show="active.status === 'offline'">Offline</span></div>
        <div class="muted small" x-text="active.user + ' · ' + active.host + ' · ' + active.os"></div>
      </div>
      <span class="grow"></span>
      <button class="btn" @click="message(activePcId)"><svg class="i i-sm" aria-hidden="true"><use href="#i-send"/></svg> Message</button>
      <button class="btn" :class="active.isolated ? 'btn-solid' : 'btn-danger'" @click="toggleIsolate(active)" x-text="active.isolated ? 'Reconnect network' : 'Cut off network'"></button>
      <button class="xbtn" style="margin-left:0" @click="closeInspector()" aria-label="Close"><svg class="i" aria-hidden="true"><use href="#i-x"/></svg></button>
    </div>
    <div class="insp-body" x-show="activePcId">
      <nav class="insp-nav" role="tablist">
        <template x-for="t in tabs" :key="t[0]"><button role="tab" :aria-selected="tab === t[0]" @click="tab = t[0]"><svg class="i"><use :href="'#i-' + t[2]"/></svg><span x-text="t[1]"></span><span class="c" x-text="t[3]"></span></button></template>
      </nav>
      <div class="insp-pane">

        <section x-show="tab === 'screen'">
          <div class="row" style="margin-bottom:14px;--gap:10px">
            <div class="seg"><button :aria-pressed="screenMode === 'snapshot'" @click="screenMode = 'snapshot'">Snapshot</button><button :aria-pressed="screenMode === 'live'" @click="goLive()" :disabled="!active.caps || !active.caps.live">Live</button></div>
            <button class="btn" @click="snapshot()" :disabled="!screenOk"><svg class="i i-sm" aria-hidden="true"><use href="#i-camera"/></svg> Take snapshot</button>
            <div class="seg" x-show="screenMonitors.length > 1"><button x-show="screenAllScreens" :aria-pressed="screenMonitorActive === 0" @click="pickMonitor(0xFFFF)">All screens</button><template x-for="m in screenMonitors" :key="m.id"><button :aria-pressed="screenMonitorActive === m.id" @click="pickMonitor(m.id)" x-text="'Monitor ' + m.id"></button></template></div>
            <div class="seg" x-show="screenMonitors.length < 2 && (active.monitors || 1) > 1"><template x-for="m in (active.monitors || 1)" :key="m"><button :aria-pressed="monitor === m - 1" @click="monitor = m - 1" x-text="'Monitor ' + m"></button></template></div>
            <span class="grow"></span>
            <button class="btn" @click="evidence()" :disabled="!screenOk">Save as evidence</button>
          </div>
          <div class="screenbox">
            <canvas x-ref="live" width="960" height="540" x-show="screenOk"></canvas>
            <div class="badge" x-show="screenOk"><span class="chip" x-show="screenMode === 'live'"><i class="dot alert"></i> Live</span><span class="chip" x-show="screenMode === 'snapshot'" x-text="'Snapshot · ' + snapAt"></span></div>
            <div class="idle" x-show="!screenOk" style="aspect-ratio:16/9"><div><b style="color:#fff;font-size:15px" x-text="active.status === 'offline' ? 'This PC is offline' : 'Screen view isn’t available'"></b><p style="margin-top:6px" x-text="active.status === 'offline' ? 'Waiting for the agent heartbeat. Screens appear again when the agent reconnects.' : 'This PC uses a Wayland session, which doesn’t allow screen capture.'"></p></div></div>
          </div>
          <div class="note warn" style="margin-top:14px" x-show="active.session === 'wayland'"><svg class="i i-sm" aria-hidden="true"><use href="#i-warn"/></svg><span>Choose “Ubuntu on Xorg” on the login screen to enable screen view on this PC.</span></div>
          <div style="margin-top:16px"><div class="label">What this PC can do</div>
            <div class="caps">
              <span class="chip" :class="active.caps && active.caps.snapshot ? 'ok' : 'off'">Snapshot</span><span class="chip" :class="active.caps && active.caps.live ? 'ok' : 'off'">Live</span>
              <span class="chip" :class="active.caps && active.caps.delta ? 'ok' : 'off'">Delta updates</span><span class="chip" :class="active.caps && active.caps.multi ? 'ok' : 'off'">Multi-monitor</span>
              <span class="chip" :class="active.caps && active.caps.secure ? 'ok' : 'off'">Secure desktop</span>
            </div></div>
          <p class="help">Students see an “instructor is viewing” indicator while Live is on. Every view is written to the audit log.</p>
          <div style="margin-top:16px" x-show="det && det.evidence && det.evidence.length">
            <div class="label">Saved evidence</div>
            <div class="stack" style="--gap:8px;margin-top:8px">
              <template x-for="ev in (det ? det.evidence : [])" :key="ev.id">
                <div class="row" style="--gap:10px;padding:8px;border:1px solid var(--border);border-radius:8px;background:var(--surface-2,transparent)">
                  <a :href="ev.url" target="_blank" rel="noopener"><img :src="ev.url" :alt="'Evidence ' + ev.id" style="height:44px;width:78px;object-fit:cover;border-radius:4px;border:1px solid var(--border);display:block"></a>
                  <div style="min-width:0">
                    <div class="small" style="font-weight:500" x-text="(ev.source_mode === 'live' ? 'Live capture' : 'Snapshot') + ' · Monitor ' + ((ev.monitor || 0) + 1)"></div>
                    <div class="tiny muted mono" x-text="ev.occurred_at + (ev.captured_by ? ' · ' + ev.captured_by : '') + ' · ' + (ev.sha256 || '').slice(0, 12)"></div>
                  </div>
                  <span class="grow"></span>
                  <a class="btn btn-sm" :href="ev.url" target="_blank" rel="noopener">Open</a>
                </div>
              </template>
            </div>
          </div>
        </section>

        <section x-show="tab === 'timeline'" x-cloak>
          <ul class="timeline"><template x-for="(ev, idx) in (det ? det.events : [])" :key="idx + '-' + ev.time"><li :class="ev.sev"><time x-text="ev.time"></time><span x-text="ev.label"></span></li></template></ul>
        </section>

        <section x-show="tab === 'alerts'" x-cloak style="padding:0">
          <table class="tbl"><thead><tr><th>Severity</th><th>Alert</th><th>Time</th><th></th></tr></thead><tbody>
            <template x-for="a in pcAlerts" :key="a.id"><tr><td><span class="sev" :class="a.sev" x-text="a.sev"></span></td><td><div class="cut" x-text="a.title"></div><div class="tiny muted mono" x-text="a.rule"></div></td><td class="num muted" x-text="a.at"></td><td class="right"><span class="small muted" x-show="a.acked">Reviewed</span><button class="btn btn-sm" x-show="!a.acked" @click="ackGroup({ items: [a], pc: a.pc, count: 1 })">Mark reviewed</button></td></tr></template>
          </tbody></table>
          <div class="empty-state" x-show="!pcAlerts.length"><b>No alerts</b>Nothing has been raised for this PC.</div>
        </section>

        <section x-show="tab === 'programs'" x-cloak style="padding:0">
          <div class="row" style="padding:12px 20px;border-bottom:1px solid var(--border)">
            <span class="small muted" x-text="det && det.processesUpdatedAt ? 'Updated ' + det.processesUpdatedAt : 'Running processes'"></span>
            <span class="grow"></span>
            <button class="btn btn-sm" @click="rescan('processes')"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Rescan</button>
            <button class="btn btn-sm" @click="promptCloseProgram()">Close a program…</button>
          </div>
          <table class="tbl"><thead><tr><th>Program</th><th class="right">CPU</th><th class="right">Memory</th><th class="right">Network</th><th></th></tr></thead><tbody>
            <template x-for="(p, idx) in (det ? det.processes : [])" :key="(p.pid || idx) + '-' + p.name"><tr>
              <td><div style="font-weight:500" :style="p.threat && 'color:var(--alert)'" x-text="p.name"></div><div class="tiny muted cut" x-text="p.title"></div></td>
              <td class="right num" x-text="p.cpu.toFixed(1) + '%'"></td><td class="right num" x-text="p.mem + ' MB'"></td><td class="right num" x-text="p.net.toFixed(1) + ' Mb/s'"></td>
              <td class="right nw"><span class="chip alert" x-show="p.threat" x-text="p.threat" style="margin-right:6px"></span><button class="btn btn-sm" @click="promptCloseProgram(p.name, p.pid || null, p.start || null)">Close</button></td></tr></template>
          </tbody></table>
        </section>

        <section x-show="tab === 'browser'" x-cloak style="padding:0">
          <div class="row" style="padding:16px 20px 8px"><span class="h3">AI traffic</span><span class="grow"></span><button class="btn btn-sm" @click="rescan('network')"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Refresh</button></div>
          <table class="tbl" x-show="det && det.aiTraffic.length"><thead><tr><th>Service</th><th>Site</th><th>Browser</th><th class="right">Requests</th><th>Last</th><th>Result</th></tr></thead><tbody>
            <template x-for="(r, idx) in (det ? det.aiTraffic : [])" :key="idx + '-' + r.host"><tr><td style="font-weight:500" x-text="r.service"></td><td class="mono small" x-text="r.host"></td><td x-text="r.proc"></td><td class="right num" x-text="r.count"></td><td class="num muted" x-text="r.last"></td><td><span class="chip" :class="r.mode === 'Blocked' ? 'ok' : 'alert'" x-text="r.mode"></span></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.aiTraffic.length" style="padding:20px"><b>No AI traffic</b>No connections to AI services were seen.</div>
          <div style="padding:16px 20px 8px" class="h3">Open tabs</div>
          <table class="tbl"><tbody><template x-for="(t, idx) in (det ? det.tabs : [])" :key="idx + '-' + t.title"><tr><td style="width:90px" class="muted" x-text="t.browser"></td><td><div x-text="t.title" style="font-weight:500"></div><div class="tiny muted mono" x-text="t.host"></div></td><td class="right"><span class="chip alert" x-show="t.ai">AI site</span></td></tr></template></tbody></table>
        </section>

        <section x-show="tab === 'installed'" x-cloak style="padding:0">
          <div class="row" style="padding:16px 20px 8px"><span class="h3">Browser extensions</span><span class="grow"></span><button class="btn btn-sm" @click="rescan('extensions')"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Rescan</button></div>
          <table class="tbl" x-show="det && det.extensions.length"><tbody><template x-for="(e, idx) in (det ? det.extensions : [])" :key="idx + '-' + e.id"><tr><td class="muted" style="width:90px" x-text="e.browser"></td><td><div style="font-weight:500" x-text="e.name"></div><div class="tiny muted mono" x-text="e.id"></div></td><td class="right"><span class="chip alert">AI assistant</span></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.extensions.length" style="padding:16px"><b>None found</b></div>
          <div class="row" style="padding:16px 20px 8px"><span class="h3">Local AI files</span><span class="grow"></span><button class="btn btn-sm" @click="rescan('artifacts')"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Rescan</button></div>
          <table class="tbl" x-show="det && det.artifacts.length"><tbody><template x-for="(a, idx) in (det ? det.artifacts : [])" :key="idx + '-' + a.path"><tr><td class="muted" style="width:90px" x-text="a.kind"></td><td><div class="mono small" x-text="a.path"></div><div class="tiny muted" x-text="a.note"></div></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.artifacts.length" style="padding:16px"><b>None found</b></div>
        </section>

        <section x-show="tab === 'network'" x-cloak style="padding:0">
          <div class="row" style="padding:16px 20px 8px"><span class="h3">Network adapters</span><span class="grow"></span><button class="btn btn-sm" @click="rescan('network')"><svg class="i i-sm" aria-hidden="true"><use href="#i-refresh"/></svg> Refresh</button></div>
          <table class="tbl"><tbody><template x-for="(a, idx) in (det ? det.adapters : [])" :key="idx + '-' + a.name"><tr><td style="font-weight:500" x-text="a.name"></td><td class="muted" x-text="a.type"></td><td class="mono small" x-text="a.ip"></td><td class="mono small muted" x-text="a.mac"></td></tr></template></tbody></table>
          <div style="padding:16px 20px 8px" class="h3">Listening ports</div>
          <table class="tbl" x-show="det && det.ports.length"><tbody><template x-for="(p, idx) in (det ? det.ports : [])" :key="idx + '-' + p.port"><tr><td class="mono" x-text="p.port + '/' + p.proto"></td><td x-text="p.proc"></td><td class="right nw"><span class="chip alert" style="margin-right:6px">Local AI port</span><button class="btn btn-sm" @click="promptCloseProgram(p.exe || p.proc, p.pid || null)">End task</button></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.ports.length" style="padding:16px"><b>Nothing unusual</b></div>
          <div style="padding:16px 20px 8px" class="h3">USB devices</div>
          <table class="tbl" x-show="det && det.devices.length"><tbody><template x-for="(d, idx) in (det ? det.devices : [])" :key="idx + '-' + d.vidpid"><tr><td class="mono small" x-text="d.vidpid"></td><td x-text="d.name"></td><td class="num muted" x-text="d.time"></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.devices.length" style="padding:16px"><b>None attached</b></div>
          <template x-if="det && det.netEvents && det.netEvents.length">
            <div>
              <div style="padding:16px 20px 8px" class="h3">Gateway DNS &amp; proxy log</div>
              <table class="tbl"><thead><tr><th>Time</th><th>Host</th><th>Layer</th><th>Category</th><th class="right">Action</th></tr></thead><tbody>
                <template x-for="(n, idx) in det.netEvents" :key="idx">
                  <tr>
                    <td class="num muted" x-text="n.occurred_at"></td>
                    <td class="mono small" x-text="n.host + (n.path ? n.path : '')"></td>
                    <td class="muted" x-text="n.layer"></td>
                    <td class="muted" x-text="n.category || '—'"></td>
                    <td class="right"><span class="chip" :class="n.action === 'blocked' ? 'ok' : (n.action === 'observed' ? 'alert' : 'watch')" x-text="n.action"></span></td>
                  </tr>
                </template>
              </tbody></table>
            </div>
          </template>
        </section>

        <section x-show="tab === 'typing'" x-cloak>
          <div class="h3">Typing pace</div><p class="muted small">Characters per minute, last 30 minutes. Only pace and pauses are measured, never which keys.</p>
          <div class="bars"><template x-for="(v, i) in (det ? det.keyrate : [])" :key="i"><i :style="'height:' + Math.min(100, v) + '%'" :title="v + ' per min'"></i></template></div>
        </section>

        <section x-show="tab === 'pastes'" x-cloak style="padding:0">
          <table class="tbl" x-show="det && det.pastes.length"><thead><tr><th>Time</th><th>Size</th><th>From</th><th>Into</th></tr></thead><tbody><template x-for="(p, idx) in (det ? det.pastes : [])" :key="idx + '-' + p.time"><tr><td class="num muted" x-text="p.time"></td><td x-text="p.chars + ' characters, ' + p.lines + ' lines'"></td><td x-text="p.from"></td><td x-text="p.to"></td></tr></template></tbody></table>
          <div class="empty-state" x-show="det && !det.pastes.length"><b>No large pastes</b>Only the size of a paste is recorded, never its text.</div>
        </section>
      </div>
    </div>
  </aside>

  <!-- Dialogs -->
  <div class="overlay" x-show="messageModal" x-cloak x-transition.opacity @click.self="messageModal = false">
    <div class="modal" role="dialog" aria-label="Send message">
      <div class="modal-head"><span class="h2" x-text="messageTarget ? 'Message ' + messageTarget.name : 'Message every PC'"></span><button class="xbtn" @click="messageModal = false" aria-label="Close"><svg class="i" aria-hidden="true"><use href="#i-x"/></svg></button></div>
      <div class="modal-body stack">
        <p class="muted">It appears on the student’s screen straight away.</p>
        <label class="field"><span>Message</span><textarea class="textarea" rows="3" placeholder="e.g. 10 minutes left. Please save your work." x-model="messageText"></textarea></label>
        <div class="pills"><button class="pill" @click="messageText = '10 minutes left. Save and push all work.'">10 minute warning</button><button class="pill" @click="messageText = 'Please close other browser tabs.'">Close tabs</button><button class="pill" @click="messageText = 'Time is up. Please stop typing.'">Time is up</button></div>
      </div>
      <div class="modal-foot"><button class="btn" @click="messageModal = false">Cancel</button><button class="btn btn-solid" @click="sendMessage()">Send</button></div>
    </div>
  </div>
  <div class="overlay" x-show="closeModal" x-cloak x-transition.opacity @click.self="closeModal = false">
    <div class="modal" role="dialog" aria-label="Close a program">
      <div class="modal-head"><span class="h2">Close this program?</span><button class="xbtn" @click="closeModal = false" aria-label="Close"><svg class="i" aria-hidden="true"><use href="#i-x"/></svg></button></div>
      <div class="modal-body stack">
        <label class="field" x-show="closeManual"><span>Program name or executable</span><input class="input mono" x-model="closeName" placeholder="e.g. ollama, code, chrome"></label>
        <p class="muted" x-show="!closeManual || closeName">The agent will ask <b x-text="closeName || ('PID ' + closePid)"></b> to quit on <b x-text="active.name || 'this PC'"></b>. Unsaved work in it may be lost.</p>
      </div>
      <div class="modal-foot"><button class="btn" @click="closeModal = false">Cancel</button><button class="btn btn-danger" @click="closeProgram()">Close program</button></div>
    </div>
  </div>
</div>
@endsection
