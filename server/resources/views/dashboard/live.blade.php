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
          <span class="font-semibold" x-text="a.pc"></span><span class="ml-auto text-xs text-slate-400" x-text="(a.occurred_at||'').substring(11,19)"></span></div>
        <div x-text="a.title"></div><div class="text-xs text-slate-500" x-text="a.user || ''"></div>
      </div>
    </template>
    <p x-show="!alerts.length" class="text-sm text-slate-500">No alerts yet.</p>
    <p class="mt-3 text-xs text-slate-400">Alerts flag events for human review. They are not proof of misconduct.</p>
  </aside>

  {{-- PC detail drawer --}}
  <div x-show="drawer" x-cloak class="fixed inset-0 z-40 flex justify-end bg-black/30" @click.self="drawer = null" @keydown.escape.window="drawer = null">
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
            <button class="btn" @click="drawer=null">✕</button>
          </div>
          <div class="my-3 flex flex-wrap gap-2">
            <button class="btn" @click="message([drawer.pc.id])">Message</button>
            <button class="btn" :class="drawer.pc.isolated ? 'btn-primary' : 'btn-danger'" @click="isolate(drawer.pc)" x-text="drawer.pc.isolated ? 'Restore network' : 'Isolate'"></button>
            <button class="btn" @click="kill(drawer.pc)">Close a program…</button>
          </div>
          <div class="mb-3 flex gap-1 border-b border-slate-200 text-sm">
            <template x-for="t in ['timeline','alerts','processes','network','typing','pastes']">
              <button class="-mb-px border-b-2 px-3 py-1.5 capitalize" :class="tab===t ? 'border-indigo-600 font-semibold' : 'border-transparent text-slate-500'" @click="tab=t" x-text="t"></button>
            </template>
          </div>
          <div x-show="tab==='timeline'"><table class="tbl"><template x-for="e in drawer.timeline"><tr><td class="w-16 font-mono text-xs" x-text="e.at"></td><td class="text-xs text-slate-500" x-text="e.type"></td><td x-text="e.summary"></td></tr></template></table></div>
          <div x-show="tab==='alerts'"><table class="tbl"><template x-for="a in drawer.alerts"><tr><td><span class="badge" :class="'sev-'+a.severity" x-text="a.severity"></span></td><td x-text="a.title"></td>
            <td class="text-xs" x-text="(a.occurred_at||'').substring(11,19)"></td><td><button class="btn btn-sm" x-show="!a.acknowledged_at" @click="ack(a)">Ack</button><span x-show="a.acknowledged_at" class="text-xs text-slate-400">acked</span></td></tr></template></table></div>
          <div x-show="tab==='processes'"><table class="tbl"><template x-for="p in drawer.processes"><tr><td class="font-mono text-xs" x-text="p.at"></td><td x-text="p.type.replace('process_','')"></td><td class="font-semibold" x-text="p.exe"></td><td class="text-xs" x-text="p.rule"></td>
            <td><button class="btn btn-sm" x-show="p.type==='process_start' && p.exe" @click="kill(drawer.pc, p.exe)">Close</button></td></tr></template></table>
            <p x-show="!drawer.processes.length" class="text-sm text-slate-500">Only processes matching AI signatures are listed.</p></div>
          <div x-show="tab==='network'"><table class="tbl"><template x-for="n in drawer.network"><tr><td class="font-mono text-xs" x-text="(n.occurred_at||'').substring(11,19)"></td>
            <td><span class="badge" :class="n.action==='blocked' ? 'sev-high' : 'sev-info'" x-text="n.action"></span></td><td x-text="n.host + (n.path||'')"></td><td class="text-xs" x-text="n.layer + ' ' + (n.category||'')"></td></tr></template></table></div>
          <div x-show="tab==='typing'">
            <div class="label">Keys per minute (timing only — no characters are recorded)</div>
            <div class="flex h-40 items-end gap-[2px] border-b border-slate-300">
              <template x-for="(h, i) in bars(drawer.keyrate.series, 'keys')"><div class="flex-1 bg-indigo-400" :style="`height:${h}%`" :title="drawer.keyrate.labels[i] + ': ' + drawer.keyrate.series[i].keys + ' keys, ' + drawer.keyrate.series[i].pastes + ' pastes'"></div></template>
            </div>
            <div class="flex justify-between text-[10px] text-slate-400"><span x-text="drawer.keyrate.labels[0]||''"></span><span x-text="drawer.keyrate.labels.at(-1)||''"></span></div>
          </div>
          <div x-show="tab==='pastes'"><table class="tbl"><tr><th>time</th><th>kind</th><th>chars</th><th>lines</th><th>code</th><th>from → to</th></tr>
            <template x-for="p in drawer.pastes"><tr><td class="font-mono text-xs" x-text="(p.occurred_at||'').substring(11,19)"></td><td x-text="p.is_copy ? 'copy' : 'paste'"></td><td x-text="p.chars"></td><td x-text="p.lines"></td>
              <td x-text="p.looks_like_code ? 'yes' : ''"></td><td class="text-xs" x-text="(p.source_app||'?') + ' → ' + (p.target_app||'?')"></td></tr></template></table>
            <p class="mt-2 text-xs text-slate-400">Clipboard contents are never stored; only size, line count and a code-likeness flag.</p></div>
        </div>
      </template>
    </div>
  </div>
</div>
@endsection
