@extends('layouts.app', ['activePage' => 'alerts.html'])
@section('title', 'Alerts')
@section('content')
@php
  $liveAlerts = isset($alerts) && $alerts->count() ? $alerts->getCollection()->map(function ($a) {
      return [
          'id' => $a->id,
          'pcId' => $a->pc_id,
          'pc' => $a->pc?->name ?? ('PC-' . $a->pc_id),
          'pcRetired' => $a->pc?->retired_at !== null,
          'user' => $a->user ?: '—',
          'sev' => $a->severity ?? 'medium',
          'title' => $a->title ?: $a->rule_id,
          'rule' => $a->rule_id ?? '',
          'cat' => $a->category ?? '',
          'at' => $a->occurred_at?->format('H:i:s') ?? '',
          'ago' => $a->occurred_at ? max(0, (int) $a->occurred_at->diffInMinutes(now())) : 0,
          'acked' => $a->acknowledged_at !== null,
          'by' => $a->acknowledger?->name ?? 'Staff',
          'note' => $a->comment ?? '',
      ];
  })->values()->all() : [];
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'sev' => $filters['severity'] ?? '',
  'unacked' => (bool) ($filters['open'] ?? false),
  'session' => (string) ($filters['session'] ?? ''),
  'byName' => auth()->user()?->name ?? 'Staff',
  'openTotal' => (int) ($openTotal ?? 0),
  'total' => isset($alerts) ? $alerts->total() : count($liveAlerts),
  'hasPages' => isset($alerts) && $alerts->hasPages(),
  'sevTotals' => $sevTotals ?? null,
  'items' => $liveAlerts,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="alertsPage" @keydown.escape.window="alertModal = false"><div class="optbar"><span class="otitle">Alerts</span><span class="osub"><span x-text="waiting"></span> waiting for review. Repeated alerts are grouped.</span><span class="grow"></span>
      <select class="select select-sm" style="width:260px" aria-label="Session" @change="selectSession($event.target.value)">
        <option value="" @selected(empty($filters['session']))>All sessions</option>
        @foreach($sessions ?? [] as $s)
          <option value="{{ $s->id }}" @selected(($filters['session'] ?? null) == $s->id)>{{ ucfirst(str_replace('_', ' ', $s->mode)) }} · {{ $s->course }}{{ $s->starts_at ? ' (' . $s->starts_at->format('M d') . ')' : '' }}</option>
        @endforeach
      </select>
    </div><div class="canvas ">
  

  <div class="toolbar">
    <div class="pills" role="group" aria-label="Severity">
      <button class="pill" :aria-pressed="sev === ''" @click="setSev('')">All <span class="n" x-text="allCount()"></span></button>
      <button class="pill" :aria-pressed="sev === 'critical'" @click="setSev('critical')">Critical <span class="n" x-text="n('critical')"></span></button>
      <button class="pill" :aria-pressed="sev === 'high'" @click="setSev('high')">High <span class="n" x-text="n('high')"></span></button>
      <button class="pill" :aria-pressed="sev === 'medium'" @click="setSev('medium')">Medium <span class="n" x-text="n('medium')"></span></button>
      <button class="pill" :aria-pressed="sev === 'low'" @click="setSev('low')">Low <span class="n" x-text="n('low')"></span></button>
    </div>
    <input class="input input-sm" style="width:220px;margin-left:8px" placeholder="Search PC or alert" x-model="q">
    <span class="grow"></span>
    <label class="check small"><span class="switch"><input type="checkbox" x-model="unacked" @change="toggleUnacked()"><i></i></span> Unreviewed only</label>
    <label class="check small"><span class="switch"><input type="checkbox" x-model="grouped"><i></i></span> Group repeats</label>
  </div>
  <div class="pills" style="margin:0 0 12px" x-show="sources.length > 1" role="group" aria-label="Filter by what detected it">
    <span class="small muted" style="align-self:center;margin-right:4px">Detected by</span>
    <button class="pill" :aria-pressed="!src ? 'true' : 'false'" @click="src = ''">All <span class="n" x-text="all.length"></span></button>
    <template x-for="s in sources" :key="s.key"><button class="pill" :aria-pressed="src === s.key ? 'true' : 'false'" @click="src = s.key"><svg class="i i-sm" aria-hidden="true"><use :href="'#' + s.icon"/></svg> <span x-text="s.label"></span> <span class="n" x-text="s.n"></span></button></template>
  </div>

  <div class="selbar" x-show="rows.some(r => !r.acked)" role="group" aria-label="Selected alerts">
    <label class="check small"><input type="checkbox" :checked="allPicked()" :indeterminate="picked.length && !allPicked()" @change="pickAll($event.target.checked)"> Select all shown</label>
    <template x-if="picked.length">
      <span class="row" style="--gap:8px;flex:1">
        <span class="small"><b x-text="picked.length"></b> selected</span>
        <input class="input input-sm" style="width:240px" placeholder="Note (optional)" x-model="pickNote" aria-label="Note for selected alerts">
        <button class="btn btn-sm btn-solid" :disabled="busy" @click="ackPicked()" x-text="'Mark ' + picked.length + ' as reviewed'"></button>
        <button class="btn btn-sm btn-quiet" @click="picked = []">Clear</button>
      </span>
    </template>
  </div>
  <template x-for="r in rows" :key="r.key">
    <div class="msg" :class="{ unread: !r.acked }">
      <div class="msg-head">
      <label class="msg-pick" :title="r.acked ? 'Already reviewed' : 'Select'"><input type="checkbox" :disabled="r.acked" :checked="isPicked(r)" @change="pick(r, $event.target.checked)" :aria-label="'Select ' + r.title + ' on ' + r.pc"></label>
      <button class="msg-row" @click="openKey = openKey === r.key ? null : r.key" :aria-expanded="openKey === r.key">
        <span class="sev" :class="r.sev" x-text="r.sev"></span>
        <span><span class="t"><span x-text="r.title"></span> <span class="count" x-show="r.count > 1" x-text="'×' + r.count"></span></span><span class="m" x-text="alertExplain(r)"></span><span class="m"><b x-text="r.pc"></b><span class="chip plain" x-show="r.pcRetired" style="margin-left:6px">retired</span> · <span x-text="r.user"></span> · <span class="chip plain"><svg class="i i-sm" aria-hidden="true"><use :href="'#' + alertSrc(r).icon"/></svg><span x-text="alertSrc(r).label"></span></span></span></span>
        <span class="when" x-text="xgAgo(r.ago)"></span>
        <span><span class="chip alert" x-show="!r.acked">Needs review</span><span class="small muted" x-show="r.acked">Reviewed</span></span>
      </button>
      </div>
      <div x-show="openKey === r.key" x-cloak>
        <template x-if="r.count > 1"><div><template x-for="i in r.items" :key="i.id"><div class="msg-sub" style="grid-template-columns:96px minmax(0,1fr) 120px auto"><span class="mono muted" x-text="i.at"></span><span x-text="i.title"></span><span class="muted" x-text="xgAgo(i.ago)"></span><button class="link-btn small" @click="openAlert(i)">Details</button></div></template></div></template>
        <div class="msg-detail" style="padding-left:20px">
          <template x-if="r.acked"><div class="stack" style="--gap:10px"><div class="note"><svg class="i i-sm" aria-hidden="true"><use href="#i-check"/></svg><div><b x-text="'Reviewed by ' + (r.by || 'Staff')"></b><br><span x-text="r.note || 'No note.'"></span></div></div><div class="row"><button class="btn" @click="openAlert(xgNewest(r.items))">Details</button></div></div></template>
          <template x-if="!r.acked">
            <div class="stack" style="--gap:10px;max-width:640px">
              <label class="field"><span>Note (optional)</span><textarea class="textarea" rows="2" placeholder="What did you find out?" x-model="notes[r.key]"></textarea></label>
              <div class="row"><button class="btn btn-solid" @click="ack(r)" x-text="r.count > 1 ? 'Mark all ' + r.count + ' as reviewed' : 'Mark as reviewed'"></button><button class="btn" @click="openAlert(xgNewest(r.items))" x-text="r.count > 1 ? 'Details (newest)' : 'Details'"></button><a class="btn btn-quiet" :href="'/#pc=' + r.pcId">Show PC in Live lab</a>
                <span class="pills" style="margin-left:auto"><button class="pill" @click="notes[r.key] = 'False alarm.'">False alarm</button><button class="pill" @click="notes[r.key] = 'Spoke to the student.'">Spoke to student</button></span></div>
            </div>
          </template>
        </div>
      </div>
    </div>
  </template>
  <div class="empty-state" x-show="!rows.length"><b>Nothing matches</b>Try clearing a filter.</div>
  @if(isset($alerts) && $alerts->total() > 0)
  <div class="panel" style="margin-top:12px">
    <div class="tbl-foot">
      <span>Showing {{ $alerts->firstItem() }}–{{ $alerts->lastItem() }} of {{ $alerts->total() }} alerts (page {{ $alerts->currentPage() }} of {{ $alerts->lastPage() }})</span>
      @if($alerts->hasPages())
        <div class="pager">
          @if($alerts->onFirstPage())
            <button disabled>Prev</button>
          @else
            <a class="btn btn-sm" href="{{ $alerts->previousPageUrl() }}">Prev</a>
          @endif
          @if($alerts->hasMorePages())
            <a class="btn btn-sm" href="{{ $alerts->nextPageUrl() }}">Next</a>
          @else
            <button disabled>Next</button>
          @endif
        </div>
      @endif
    </div>
  </div>
  @endif
  @include('alerts._details-modal')
</div></div>
@endsection
