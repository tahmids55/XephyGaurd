@extends('layouts.app')
@section('title', 'Live lab')
@section('content')
<div x-data="labView(@js(['labId' => $lab->id, 'cols' => $lab->grid_cols, 'rows' => $lab->grid_rows, 'pcs' => $pcs, 'seats' => $seats, 'alerts' => $recentAlerts]))" class="grid grid-cols-1 gap-4 xl:grid-cols-[1fr_360px]">
  <section class="space-y-4">
    {{-- session bar --}}
    <div class="card flex flex-wrap items-center gap-3">
      @if ($session)
        <span class="badge mode-{{ $session->mode }}">{{ strtoupper(str_replace('_', '-', $session->mode)) }}</span>
        <div><div class="font-semibold">{{ $session->course }}</div>
          <div class="text-xs text-slate-500">{{ $session->profile->name }} · since {{ $session->started_at->format('H:i') }}{{ $session->ends_at ? ' · ends '.$session->ends_at->format('H:i') : '' }}</div></div>
        <form method="post" action="{{ route('sessions.switch', $session) }}" class="flex items-center gap-2">@csrf
          <select name="profile_id" class="input w-56">@foreach ($profiles as $p)<option value="{{ $p->id }}" @selected($p->id === $session->profile_id)>{{ $p->name }}</option>@endforeach</select>
          <button class="btn">Switch</button></form>
        <form method="post" action="{{ route('sessions.stop', $session) }}" onsubmit="return confirm('End the session? Blocking relaxes immediately.')">@csrf<button class="btn btn-danger">End session</button></form>
      @else
        <span class="badge mode-always_on">ALWAYS-ON</span>
        <form method="post" action="{{ route('sessions.start') }}" class="flex flex-wrap items-center gap-2">@csrf
          <input name="course" class="input w-40" placeholder="Course (e.g. CS101)" required>
          <select name="profile_id" class="input w-56">@foreach ($profiles->where('mode', '!=', 'always_on') as $p)<option value="{{ $p->id }}">{{ $p->name }}</option>@endforeach</select>
          <input name="duration_min" type="number" min="5" class="input w-28" placeholder="Minutes">
          <button class="btn btn-primary">Start session</button></form>
      @endif
      <div class="ml-auto flex items-center gap-2 text-sm">
        <span class="badge bg-emerald-100 text-emerald-800" x-text="counts.clean + ' clean'"></span>
        <span class="badge bg-amber-100 text-amber-800" x-text="counts.warning + ' warning'"></span>
        <span class="badge bg-red-100 text-red-700" x-text="counts.alert + ' alert'"></span>
        <span class="badge bg-slate-200 text-slate-600" x-text="counts.offline + ' offline'"></span>
        <button class="btn" @click="message(null)">Message all</button>
        <button class="btn" @click="editing ? saveLayout() : editing = true" x-text="editing ? 'Save seat map' : 'Edit seat map'"></button>
      </div>
    </div>

    {{-- seat map --}}
    <div class="card overflow-x-auto">
      <div x-show="editing" class="mb-3 flex items-center gap-3 text-sm">
        <span class="text-slate-500">Drag PCs onto seats. Grid:</span>
        <input type="number" min="1" max="30" x-model.number="cols" class="input w-20"> ×
        <input type="number" min="1" max="30" x-model.number="rows" class="input w-20">
      </div>
      <div class="grid gap-2" :style="`grid-template-columns: repeat(${cols}, minmax(120px, 1fr))`">
        <template x-for="y in rows" :key="'r'+y">
          <template x-for="x in cols" :key="'c'+x+'-'+y">
            <div class="relative min-h-[92px] cursor-pointer rounded-lg border-2 p-2 text-xs transition"
                 :class="tileClass(cell(x-1, y-1))"
                 @dragover.prevent @drop="drop(x-1, y-1)"
                 :draggable="editing && !!cell(x-1,y-1)" @dragstart="dragStart(cell(x-1,y-1)?.id)"
                 @click="cell(x-1,y-1) && open(cell(x-1,y-1).id)">
              <template x-if="cell(x-1, y-1)">
                <div>
                  <div class="flex items-center justify-between"><span class="font-bold" x-text="cell(x-1,y-1).name"></span>
                    <span class="font-mono text-[11px]" x-text="cell(x-1,y-1).score"></span></div>
                  <div class="truncate text-slate-600" x-text="cell(x-1,y-1).user || '—'"></div>
                  <div class="truncate" x-text="cell(x-1,y-1).app || ''"></div>
                  <div class="truncate text-[11px] text-slate-500" x-text="cell(x-1,y-1).last_event || ''"></div>
                  <span x-show="cell(x-1,y-1).isolated" class="badge absolute top-1 right-1 bg-red-600 text-white">isolated</span>
                </div>
              </template>
              <template x-if="!cell(x-1, y-1)"><div class="text-slate-300" x-text="String.fromCharCode(64+y) + x"></div></template>
            </div>
          </template>
        </template>
      </div>
      <div class="mt-4" x-show="unplaced.length">
        <div class="label">Not placed on the seat map</div>
        <div class="flex flex-wrap gap-2 rounded-lg border border-dashed border-slate-300 p-2" @dragover.prevent @drop="unplace()">
          <template x-for="p in unplaced" :key="p.id">
            <div class="min-w-[120px] cursor-pointer rounded-lg border-2 p-2 text-xs" :class="tileClass(p)" :draggable="editing"
                 @dragstart="dragStart(p.id)" @click="open(p.id)">
              <div class="font-bold" x-text="p.name"></div><div x-text="p.user || '—'"></div>
              <div class="truncate text-[11px] text-slate-500" x-text="p.last_event || ''"></div>
            </div>
          </template>
        </div>
      </div>
      @if ($pcs->isEmpty())
        <p class="text-sm text-slate-500">No PCs enrolled yet. Go to <a class="text-indigo-600 underline" href="{{ route('enroll.index') }}">Add a PC</a>.</p>
      @endif
    </div>
  </section>

  {{-- alerts feed --}}
  <aside class="card h-fit max-h-[80vh] overflow-y-auto">
    <div class="mb-2 flex items-center justify-between"><h2 class="font-semibold">Alerts</h2><a href="{{ route('alerts.index') }}" class="text-xs text-indigo-600">all →</a></div>
    <template x-for="a in alerts" :key="a.id">
      <div class="border-b border-slate-100 py-2 text-sm">
        <div class="flex items-center gap-2"><span class="badge" :class="'sev-'+a.severity" x-text="a.severity"></span>
          <span class="font-semibold" x-text="a.pc"></span><span class="ml-auto text-xs text-slate-400" x-text="fmtTime(a.occurred_at)"></span></div>
        <div x-text="a.title"></div><div class="text-xs text-slate-500" x-text="a.user || ''"></div>
      </div>
    </template>
    <p x-show="!alerts.length" class="text-sm text-slate-500">No alerts yet.</p>
    <p class="mt-3 text-xs text-slate-400">Alerts flag events for human review. They are not proof of misconduct.</p>
  </aside>

  {{-- PC detail drawer --}}
  <div x-show="drawer" x-cloak class="fixed inset-0 z-40 flex justify-end bg-black/30" @click.self="closeDrawer()" @keydown.escape.window="closeDrawer()">
    <div class="h-full w-full max-w-2xl overflow-y-auto bg-white p-5 shadow-xl" x-show="drawer">
      <template x-if="drawer">
        <div>
          <div class="flex items-start justify-between">
            <div><h2 class="text-xl font-bold" x-text="drawer.pc.name"></h2>
              <div class="text-sm text-slate-500"><span x-text="drawer.pc.hostname"></span> · <span x-text="drawer.pc.ip"></span> · <span x-text="drawer.pc.os"></span> · agent <span x-text="drawer.pc.agent_version"></span></div>
              <div class="text-sm">User: <b x-text="drawer.pc.user || '—'"></b> · mode <span x-text="drawer.pc.mode"></span> · live score <b x-text="drawer.pc.score"></b></div>
              <div class="mt-1 text-xs text-slate-500" x-show="drawer.pc.capabilities">
                Can see: <template x-for="(v,k) in drawer.pc.capabilities"><span class="mr-1" :class="v ? 'text-emerald-700' : 'text-red-600 line-through'" x-text="k"></span></template>
              </div></div>
            <button class="btn" @click="closeDrawer()">✕</button>
          </div>
          <div class="my-3 flex flex-wrap gap-2">
            <button class="btn" @click="message([drawer.pc.id])">Message</button>
            <button class="btn" :class="drawer.pc.isolated ? 'btn-primary' : 'btn-danger'" @click="isolate(drawer.pc)" x-text="drawer.pc.isolated ? 'Restore network' : 'Isolate'"></button>
            <button class="btn" @click="kill(drawer.pc)">Close a program…</button>
          </div>
          <div class="mb-3 flex flex-wrap gap-1 border-b border-slate-200 text-sm">
            <template x-for="t in ['screen','timeline','alerts','processes','browser','installed','network','typing','pastes']">
              <button class="-mb-px border-b-2 px-3 py-1.5 capitalize" :class="tab===t ? 'border-indigo-600 font-semibold' : 'border-transparent text-slate-500'" @click="switchTab(t)" x-text="t"></button>
            </template>
          </div>
          <div x-show="tab==='screen'" class="space-y-4">
            <div class="flex flex-wrap items-center justify-between gap-2 p-3 bg-slate-50 rounded border border-slate-200">
              <div class="space-y-1">
                <div class="text-xs font-semibold text-slate-700">Capabilities & Controls</div>
                <div class="flex flex-wrap items-center gap-1.5 text-xs">
                  <template x-if="drawer.pc.capabilities?.session_type === 'wayland'">
                    <span class="badge bg-red-100 text-red-700 font-semibold">Wayland (Unsupported)</span>
                  </template>
                  <template x-if="drawer.pc.capabilities?.session_type !== 'wayland'">
                    <div class="flex flex-wrap gap-1">
                      <span class="badge bg-slate-200 text-slate-700 font-mono" x-text="drawer.pc.capabilities?.os || drawer.pc.os"></span>
                      <span class="badge" :class="drawer.pc.capabilities?.snapshot !== false ? 'bg-emerald-100 text-emerald-800' : 'bg-slate-100 text-slate-400'">Snapshot</span>
                      <span class="badge" :class="drawer.pc.capabilities?.live !== false ? 'bg-emerald-100 text-emerald-800' : 'bg-slate-100 text-slate-400'">Live</span>
                      <span class="badge" :class="drawer.pc.capabilities?.delta !== false ? 'bg-emerald-100 text-emerald-800' : 'bg-slate-100 text-slate-400'">Delta</span>
                      <span class="badge" :class="drawer.pc.capabilities?.multi_monitor ? 'bg-emerald-100 text-emerald-800' : 'bg-slate-100 text-slate-400'">Multi-Monitor</span>
                      <span class="badge" :class="drawer.pc.capabilities?.secure_desktop ? 'bg-indigo-100 text-indigo-800' : 'bg-slate-100 text-slate-400'">Secure Desktop</span>
                    </div>
                  </template>
                </div>
              </div>
              <div class="flex items-center gap-2">
                <button type="button" class="btn btn-sm" :disabled="drawer.pc.capabilities?.session_type === 'wayland'" @click="startScreen('snapshot')">
                  Snapshot
                </button>
                <button type="button" class="btn btn-sm" :class="screenStreaming && screenMode === 'live' ? 'btn-danger' : 'btn-primary'" :disabled="drawer.pc.capabilities?.session_type === 'wayland'" @click="screenStreaming && screenMode === 'live' ? stopScreen() : startScreen('live')" x-text="screenStreaming && screenMode === 'live' ? 'Stop Live' : 'Live Stream'">
                </button>
              </div>
            </div>

            <div class="flex items-center justify-between text-xs font-mono text-slate-500">
              <span x-text="screenStatus"></span>
              <span x-show="screenStreaming" class="inline-flex items-center gap-1.5 font-sans font-semibold text-xs" :class="screenMode === 'live' ? 'text-emerald-600' : 'text-indigo-600'">
                <span class="inline-block w-2 h-2 rounded-full animate-pulse" :class="screenMode === 'live' ? 'bg-emerald-500' : 'bg-indigo-500'"></span>
                <span x-text="screenMode === 'live' ? 'LIVE FEED' : 'SNAPSHOT'"></span>
              </span>
            </div>

            <div class="relative bg-slate-950 rounded-lg overflow-hidden border border-slate-800 flex items-center justify-center min-h-[340px]">
              <canvas id="xg-screen-canvas" class="w-full max-h-[500px] object-contain block"></canvas>
              <div x-show="!screenStreaming && !screenHasFrame" class="absolute text-slate-500 text-sm">
                No active screen feed. Click Snapshot or Live Stream to start.
              </div>
              <div x-show="screenStreaming && !screenHasFrame" class="absolute flex flex-col items-center gap-2 text-slate-300 text-sm bg-slate-900/80 px-4 py-3 rounded-lg border border-slate-700">
                <svg class="animate-spin h-5 w-5 text-indigo-400" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24">
                  <circle class="opacity-25" cx="12" cy="12" r="10" stroke="currentColor" stroke-width="4"></circle>
                  <path class="opacity-75" fill="currentColor" d="M4 12a8 8 0 018-8v8H4z"></path>
                </svg>
                <span x-text="screenStatus || 'Connecting to screen feed…'"></span>
              </div>
            </div>
          </div>
          <div x-show="tab==='timeline'"><table class="tbl"><template x-for="e in drawer.timeline"><tr><td class="w-16 font-mono text-xs" x-text="e.at"></td><td class="text-xs text-slate-500" x-text="e.type"></td><td x-text="e.summary"></td></tr></template></table></div>
          <div x-show="tab==='alerts'"><table class="tbl"><template x-for="a in drawer.alerts"><tr><td><span class="badge" :class="'sev-'+a.severity" x-text="a.severity"></span></td><td x-text="a.title"></td>
            <td class="text-xs" x-text="fmtTime(a.occurred_at)"></td><td><button class="btn btn-sm" x-show="!a.acknowledged_at" @click="ack(a)">Ack</button><span x-show="a.acknowledged_at" class="text-xs text-slate-400">acked</span></td></tr></template></table></div>
          <div x-show="tab==='processes'" class="space-y-3">
            <div class="flex flex-wrap items-center gap-2">
              <input type="search" x-model="procSearch" placeholder="Search process, PID, or user…" class="input w-48 text-xs">
              <div class="flex rounded border border-slate-200 text-xs">
                <button type="button" class="px-2 py-1" :class="procFilter==='all' ? 'bg-indigo-600 text-white font-semibold' : 'text-slate-600'" @click="procFilter='all'">All (<span x-text="(drawer.running_processes||[]).length"></span>)</button>
                <button type="button" class="border-l border-slate-200 px-2 py-1" :class="procFilter==='user' ? 'bg-indigo-600 text-white font-semibold' : 'text-slate-600'" @click="procFilter='user'">User Apps</button>
                <button type="button" class="border-l border-slate-200 px-2 py-1" :class="procFilter==='flagged' ? 'bg-red-600 text-white font-semibold' : 'text-slate-600'" @click="procFilter='flagged'">AI Flagged</button>
              </div>
              <div class="flex items-center rounded border border-slate-200 text-xs">
                <span class="bg-slate-50 px-2 py-1 text-[11px] text-slate-500">Sort:</span>
                <button type="button" class="border-l border-slate-200 px-2 py-1" :class="procSort==='ram' ? 'bg-slate-800 text-white font-semibold' : 'text-slate-600'" @click="setProcSort('ram')">RAM <span x-show="procSort==='ram'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></button>
                <button type="button" class="border-l border-slate-200 px-2 py-1" :class="procSort==='cpu' ? 'bg-slate-800 text-white font-semibold' : 'text-slate-600'" @click="setProcSort('cpu')">CPU <span x-show="procSort==='cpu'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></button>
                <button type="button" class="border-l border-slate-200 px-2 py-1" :class="procSort==='net' ? 'bg-slate-800 text-white font-semibold' : 'text-slate-600'" @click="setProcSort('net')">Net <span x-show="procSort==='net'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></button>
              </div>
              <button type="button" class="btn btn-sm ml-auto" @click="rescan(drawer.pc, 'processes')">↻ Refresh</button>
            </div>
            <div class="text-[11px] text-slate-400" x-show="drawer.processes_updated_at">
              Live snapshot at <span class="font-mono" x-text="drawer.processes_updated_at"></span>
            </div>
            <div class="max-h-80 overflow-y-auto border border-slate-100 rounded">
              <table class="tbl">
                <thead>
                  <tr>
                    <th>PID</th>
                    <th>Process</th>
                    <th>User</th>
                    <th class="cursor-pointer select-none hover:text-indigo-600" @click="setProcSort('cpu')">CPU <span x-show="procSort==='cpu'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></th>
                    <th class="cursor-pointer select-none hover:text-indigo-600" @click="setProcSort('ram')">RAM <span x-show="procSort==='ram'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></th>
                    <th class="cursor-pointer select-none hover:text-indigo-600" @click="setProcSort('net')">Network <span x-show="procSort==='net'" x-text="procSortDir==='desc' ? '↓' : '↑'"></span></th>
                    <th>Rule</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  <template x-for="p in filteredProcesses()" :key="p.pid">
                    <tr :class="p.rule_id ? 'bg-red-50/70' : ''">
                      <td class="font-mono text-xs text-slate-500" x-text="p.pid"></td>
                      <td>
                        <div class="font-semibold text-xs" x-text="p.exe || p.name"></div>
                        <div class="text-[10px] text-slate-400" x-show="p.name && p.name !== p.exe" x-text="p.name"></div>
                      </td>
                      <td class="text-xs text-slate-600" x-text="p.user || '—'"></td>
                      <td class="font-mono text-xs" :class="(p.cpu_pct || 0) >= 20 ? 'text-amber-600 font-semibold' : 'text-slate-600'" x-text="Number(p.cpu_pct || 0).toFixed(1) + '%'"></td>
                      <td class="font-mono text-xs text-slate-600" x-text="(p.rss_mb || 0) + ' MB'"></td>
                      <td class="font-mono text-xs text-slate-600">
                        <span :class="(p.net_kbps || 0) > 0 ? 'text-indigo-600 font-semibold' : ''" x-text="Number(p.net_kbps || 0).toFixed(1) + ' KB/s'"></span>
                        <span class="text-[10px] text-slate-400" x-show="(p.net_conns || 0) > 0" x-text="'(' + p.net_conns + ')'"></span>
                      </td>
                      <td><span x-show="p.rule_id" class="badge sev-high text-[10px]" x-text="p.rule_id"></span></td>
                      <td class="text-right"><button class="btn btn-sm text-xs" @click="kill(drawer.pc, p.exe, p.pid)">End task</button></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!filteredProcesses().length" class="p-3 text-xs text-slate-500">No matching running processes in the current snapshot. Click <b>Refresh</b> to request an immediate scan.</p>
            </div>
            <div x-show="(drawer.processes||[]).length" class="pt-2 border-t border-slate-200">
              <div class="label mb-1">Flagged AI Process History (Session Log)</div>
              <table class="tbl"><template x-for="p in drawer.processes"><tr><td class="font-mono text-xs" x-text="p.at"></td><td class="text-xs" x-text="p.type.replace('process_','')"></td><td class="font-semibold text-xs" x-text="p.exe"></td><td class="text-xs text-red-600" x-text="p.rule"></td>
                <td class="text-right"><button class="btn btn-sm text-xs" x-show="p.type==='process_start' && p.exe" @click="kill(drawer.pc, p.exe, p.pid)">Close</button></td></tr></template></table>
            </div>
          </div>
          <div x-show="tab==='browser'" class="space-y-4">
            <div class="flex items-center justify-between">
              <div>
                <div class="label">Browser Tabs &amp; Live Outbound AI Network Streams</div>
                <div class="text-[11px] text-slate-400">Tracks active browser tab titles (WinEventHook) and real-time AI prompt/completion streams (OS DNS + TLS SNI + Kernel Per-Socket Bytes).</div>
              </div>
              <button type="button" class="btn btn-sm" @click="rescan(drawer.pc, 'browser')">↻ Refresh</button>
            </div>

            <div class="border border-slate-100 rounded p-3">
              <div class="flex items-center justify-between mb-1">
                <div class="text-xs font-semibold text-slate-700">Live Outbound AI Network Streams (<span x-text="(drawer.ai_traffic||[]).length"></span>)</div>
                <div class="text-[10px] text-slate-400">OS DNS + TLS SNI + Kernel TCP EStats (Keep-Alive Filtered)</div>
              </div>
              <table class="tbl" x-show="(drawer.ai_traffic||[]).length">
                <thead><tr><th>Time</th><th>Process</th><th>AI Domain</th><th>Traffic</th><th>Rule</th></tr></thead>
                <tbody>
                  <template x-for="tr in drawer.ai_traffic">
                    <tr class="bg-red-50/70">
                      <td class="font-mono text-xs text-slate-500" x-text="tr.at"></td>
                      <td class="font-semibold text-xs" x-text="tr.process || '—'"></td>
                      <td>
                        <div class="font-mono text-xs font-semibold text-indigo-700" x-text="tr.domain"></div>
                        <div class="font-mono text-[10px] text-slate-400" x-show="tr.remote_ip" x-text="tr.remote_ip + ':443'"></div>
                      </td>
                      <td class="font-mono text-xs text-slate-700">
                        <span class="text-amber-700" x-text="'↑ ' + Number(tr.sent_kb||0).toFixed(1) + ' KB'"></span>
                        <span class="mx-1 text-slate-300">·</span>
                        <span class="text-emerald-700" x-text="'↓ ' + Number(tr.recv_kb||0).toFixed(1) + ' KB'"></span>
                      </td>
                      <td><span class="badge sev-high text-[10px]" x-text="tr.rule_id"></span></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.ai_traffic||[]).length" class="text-xs text-slate-400">No active outbound AI prompt/completion streams detected in this session window.</p>
            </div>

            <div class="border border-slate-100 rounded p-3">
              <div class="flex items-center justify-between mb-1">
                <div class="text-xs font-semibold text-slate-700">Active Browser Tabs &amp; Foreground Windows (<span x-text="(drawer.browser_tabs||[]).length"></span>)</div>
              </div>
              <div class="max-h-72 overflow-y-auto">
                <table class="tbl" x-show="(drawer.browser_tabs||[]).length">
                  <thead><tr><th>Time</th><th>App / Browser</th><th>Tab / Window Title</th><th>Status</th></tr></thead>
                  <tbody>
                    <template x-for="bt in drawer.browser_tabs">
                      <tr :class="bt.ai_site ? 'bg-red-50/80' : ''">
                        <td class="font-mono text-xs text-slate-500 whitespace-nowrap" x-text="bt.at"></td>
                        <td class="text-xs font-semibold uppercase text-slate-600 whitespace-nowrap" x-text="bt.app || '—'"></td>
                        <td class="text-xs text-slate-800 break-all" x-text="bt.title || bt.app || '—'"></td>
                        <td class="whitespace-nowrap">
                          <span x-show="bt.ai_site" class="badge sev-high text-[10px]" x-text="'AI TAB · ' + bt.ai_site"></span>
                          <span x-show="!bt.ai_site && ['chrome','msedge','brave','firefox','opera'].includes((bt.app||'').toLowerCase())" class="badge sev-info text-[10px]">Browser Tab</span>
                          <span x-show="!bt.ai_site && !['chrome','msedge','brave','firefox','opera'].includes((bt.app||'').toLowerCase())" class="badge sev-low text-[10px]">Window</span>
                        </td>
                      </tr>
                    </template>
                  </tbody>
                </table>
              </div>
              <p x-show="!(drawer.browser_tabs||[]).length" class="text-xs text-slate-400">No browser tab or window focus events recorded yet.</p>
            </div>
          </div>
          <div x-show="tab==='installed'" class="space-y-4">
            <div class="flex items-center justify-between">
              <div>
                <div class="label">Pre-Installed / On-Disk AI Inventory</div>
                <div class="text-[11px] text-slate-400">Storage &amp; installed tools are shown here for recognition only. They do not raise alerts unless actively used or launched.</div>
              </div>
              <button type="button" class="btn btn-sm" @click="rescan(drawer.pc, 'extensions')">↻ Refresh</button>
            </div>

            <div class="border border-slate-100 rounded p-3">
              <div class="flex items-center justify-between mb-1">
                <div class="text-xs font-semibold text-slate-700">IDE &amp; Browser AI Extensions (<span x-text="(drawer.extensions||[]).length"></span> AI / <span x-text="drawer.extensions_total||0"></span> total)</div>
                <div class="text-[10px] text-slate-400" x-show="drawer.extensions_updated_at">Scanned at <span class="font-mono" x-text="drawer.extensions_updated_at"></span></div>
              </div>
              <table class="tbl" x-show="(drawer.extensions||[]).length">
                <thead><tr><th>IDE / Browser</th><th>Extension</th><th>User</th><th>Status</th><th>Rule</th></tr></thead>
                <tbody>
                  <template x-for="ex in drawer.extensions" :key="ex.ide + ':' + ex.id + ':' + ex.user">
                    <tr :class="ex.active ? 'bg-red-50/80' : ''">
                      <td class="text-xs font-semibold uppercase text-slate-600" x-text="ex.ide"></td>
                      <td>
                        <div class="font-semibold text-xs" x-text="ex.description || ex.id"></div>
                        <div class="font-mono text-[10px] text-slate-400" x-text="ex.id + (ex.version ? ' (' + ex.version + ')' : '')"></div>
                      </td>
                      <td class="text-xs text-slate-600" x-text="ex.user || '—'"></td>
                      <td>
                        <span x-show="ex.active" class="badge sev-high text-[10px]" x-text="'ACTIVE NOW' + (ex.activity ? ' · ' + ex.activity : '')"></span>
                        <span x-show="!ex.active" class="badge sev-low text-[10px]">Installed (Idle)</span>
                      </td>
                      <td><span class="font-mono text-[11px] text-slate-500" x-text="ex.rule_id"></span></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.extensions||[]).length" class="text-xs text-slate-400">No AI extensions installed on this PC.</p>
            </div>

            <div class="border border-slate-100 rounded p-3">
              <div class="flex items-center justify-between mb-1">
                <div class="text-xs font-semibold text-slate-700">Local AI Model Files &amp; Caches on Disk (<span x-text="(drawer.artifacts||[]).length"></span> found)</div>
                <div class="text-[10px] text-slate-400" x-show="drawer.artifacts_updated_at">Scanned at <span class="font-mono" x-text="drawer.artifacts_updated_at"></span> (<span x-text="drawer.artifacts_visited||0"></span> files checked)</div>
              </div>
              <table class="tbl" x-show="(drawer.artifacts||[]).length">
                <thead><tr><th>Model File / Store Path</th><th>Type</th><th>Size</th><th>Status</th><th>Rule</th></tr></thead>
                <tbody>
                  <template x-for="art in drawer.artifacts" :key="art.path">
                    <tr>
                      <td class="font-mono text-xs break-all text-slate-700" x-text="art.path"></td>
                      <td class="text-xs text-slate-600" x-text="art.description || 'Local AI model'"></td>
                      <td class="font-mono text-xs text-slate-600 whitespace-nowrap" x-text="(art.size_mb || 0) + ' MB'"></td>
                      <td><span class="badge sev-low text-[10px]">On Disk (Storage)</span></td>
                      <td><span class="font-mono text-[11px] text-slate-500" x-text="art.rule_id"></span></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.artifacts||[]).length" class="text-xs text-slate-400">No local AI model files (`.gguf`, `.safetensors`, `.onnx`, `.ollama`, `.lmstudio`, HuggingFace cache) found on disk.</p>
            </div>
          </div>
          <div x-show="tab==='network'" class="space-y-4">
            <div class="flex items-center justify-between">
              <div class="label">Network Adapters, AI Ports &amp; External Devices</div>
              <button type="button" class="btn btn-sm" @click="rescan(drawer.pc, 'network')">↻ Refresh</button>
            </div>
            <div>
              <div class="text-xs font-semibold text-slate-600 mb-1">Network Interfaces &amp; Tethering / VPN</div>
              <table class="tbl" x-show="(drawer.adapters||[]).length">
                <thead><tr><th>Time</th><th>Change</th><th>Interface</th><th>IP / Route</th><th>Rule</th></tr></thead>
                <tbody>
                  <template x-for="a in drawer.adapters">
                    <tr :class="a.kind ? 'bg-amber-50/60' : ''">
                      <td class="font-mono text-xs text-slate-500" x-text="a.at"></td>
                      <td><span class="badge" :class="a.up ? 'sev-info' : 'sev-low'" x-text="a.change"></span></td>
                      <td>
                        <div class="font-semibold text-xs" x-text="a.name"></div>
                        <div class="text-[10px] text-slate-400" x-text="a.description || a.driver"></div>
                      </td>
                      <td class="font-mono text-xs text-slate-600">
                        <span x-text="(a.addresses||[]).join(', ') || '—'"></span>
                        <span x-show="a.default_route" class="ml-1 rounded bg-slate-200 px-1 text-[10px]">default-gw</span>
                      </td>
                      <td><span x-show="a.kind" class="badge sev-high text-[10px]" x-text="a.kind"></span></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.adapters||[]).length" class="text-xs text-slate-400">No network adapter events yet.</p>
            </div>
            <div>
              <div class="text-xs font-semibold text-slate-600 mb-1">Detected Local LLM / AI Listening Ports</div>
              <table class="tbl" x-show="(drawer.listening_ports||[]).length">
                <thead><tr><th>Time</th><th>Port</th><th>Process</th><th>Rule</th><th></th></tr></thead>
                <tbody>
                  <template x-for="lp in drawer.listening_ports">
                    <tr class="bg-red-50/70">
                      <td class="font-mono text-xs text-slate-500" x-text="lp.at"></td>
                      <td class="font-mono text-xs font-bold" x-text="(lp.address || '127.0.0.1') + ':' + lp.port"></td>
                      <td class="text-xs"><span class="font-semibold" x-text="lp.process || 'unknown'"></span> <span class="font-mono text-slate-400" x-show="lp.pid" x-text="'(PID ' + lp.pid + ')'"></span></td>
                      <td><span class="badge sev-high text-[10px]" x-text="lp.rule_id + (lp.probed ? ' (probed)' : '')"></span></td>
                      <td class="text-right"><button class="btn btn-sm text-xs" x-show="lp.pid || lp.process" @click="kill(drawer.pc, lp.process, lp.pid)">End task</button></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.listening_ports||[]).length" class="text-xs text-slate-400">No flagged local LLM listening ports detected.</p>
            </div>
            <div>
              <div class="text-xs font-semibold text-slate-600 mb-1">USB Storage &amp; External Devices (Phones / Drives)</div>
              <table class="tbl" x-show="(drawer.devices||[]).length">
                <thead><tr><th>Time</th><th>Action</th><th>Class</th><th>Device / Vendor</th><th>Rule</th></tr></thead>
                <tbody>
                  <template x-for="d in drawer.devices">
                    <tr :class="d.rule_id ? 'bg-amber-50/70' : ''">
                      <td class="font-mono text-xs text-slate-500" x-text="d.at"></td>
                      <td class="text-xs font-semibold" x-text="d.action"></td>
                      <td class="text-xs" x-text="d.class"></td>
                      <td class="text-xs"><span x-text="d.model || 'USB device'"></span> <span class="font-mono text-[10px] text-slate-400" x-show="d.vendor" x-text="'(' + d.vendor + ':' + d.product + ')'"></span></td>
                      <td><span x-show="d.rule_id" class="badge sev-medium text-[10px]" x-text="d.rule_id"></span></td>
                    </tr>
                  </template>
                </tbody>
              </table>
              <p x-show="!(drawer.devices||[]).length" class="text-xs text-slate-400">No USB storage or phone events recorded.</p>
            </div>
            <div x-show="(drawer.network||[]).length">
              <div class="text-xs font-semibold text-slate-600 mb-1">Gateway DNS / Proxy Events</div>
              <table class="tbl"><template x-for="n in drawer.network"><tr><td class="font-mono text-xs" x-text="fmtTime(n.occurred_at)"></td>
                <td><span class="badge" :class="n.action==='blocked' ? 'sev-high' : 'sev-info'" x-text="n.action"></span></td><td x-text="n.host + (n.path||'')"></td><td class="text-xs" x-text="n.layer + ' ' + (n.category||'')"></td></tr></template></table>
            </div>
          </div>
          <div x-show="tab==='typing'">
            <div class="label">Keys per minute (timing only — no characters are recorded)</div>
            <div class="flex h-40 items-end gap-[2px] border-b border-slate-300">
              <template x-for="(h, i) in bars(drawer.keyrate.series, 'keys')"><div class="flex-1 bg-indigo-400" :style="`height:${h}%`" :title="drawer.keyrate.labels[i] + ': ' + drawer.keyrate.series[i].keys + ' keys, ' + drawer.keyrate.series[i].pastes + ' pastes'"></div></template>
            </div>
            <div class="flex justify-between text-[10px] text-slate-400"><span x-text="drawer.keyrate.labels[0]||''"></span><span x-text="drawer.keyrate.labels.at(-1)||''"></span></div>
          </div>
          <div x-show="tab==='pastes'"><table class="tbl"><tr><th>time</th><th>kind</th><th>chars</th><th>lines</th><th>code</th><th>from → to</th></tr>
            <template x-for="p in drawer.pastes"><tr><td class="font-mono text-xs" x-text="fmtTime(p.occurred_at)"></td><td x-text="p.is_copy ? 'copy' : 'paste'"></td><td x-text="p.chars"></td><td x-text="p.lines"></td>
              <td x-text="p.looks_like_code ? 'yes' : ''"></td><td class="text-xs" x-text="(p.source_app||'?') + ' → ' + (p.target_app||'?')"></td></tr></template></table>
            <p class="mt-2 text-xs text-slate-400">Clipboard contents are never stored; only size, line count and a code-likeness flag.</p></div>
        </div>
      </template>
    </div>
  </div>
</div>
@endsection
