@extends('layouts.app', ['activePage' => 'share.html'])
@section('title', 'Shared clipboard')
@section('content')
@php
  $initialHist = !empty($history) ? $history : [];
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'text' => $text ?? '',
  'last' => $updatedAt ?? 'Never',
  'hist' => $initialHist,
  'userName' => auth()->user()->name,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="sharePage">
  <div class="optbar"><span class="otitle">Shared clipboard</span><span class="osub">Send text, commands or links to every PC in one step.</span><span class="grow"></span></div><div class="canvas narrow">
  
  <section class="panel" style="border-radius:0">
    <div class="panel-head"><h2 class="h2">New share</h2><span class="small muted" x-text="'Last: ' + last"></span></div>
    <div class="panel-body stack">
      <textarea class="textarea" :class="cmd && 'mono'" rows="5" placeholder="Paste text, a command, or a link…" x-model="text" @focus="editing = true" @blur="editing = false"></textarea>
      <div class="row"><label class="check"><span class="switch"><input type="checkbox" x-model="cmd"><i></i></span> Show as a command</label><span class="grow"></span>
        <button class="btn" @click="text = ''">Clear</button><button class="btn btn-solid" @click="send()"><svg class="i i-sm" aria-hidden="true"><use href="#i-send"/></svg> Share with all PCs</button></div>
    </div>
  </section>
  <section class="panel" style="border-radius:0">
    <div class="panel-head"><h2 class="h2">Recent shares</h2></div>
    <template x-for="h in hist" :key="h.t + h.v"><div class="row" style="padding:12px 20px;border-bottom:1px solid var(--border);--gap:14px;flex-wrap:nowrap"><span class="mono small muted" x-text="h.t"></span><span class="grow" style="min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap" :class="h.cmd && 'mono small'" x-text="h.v"></span><button class="btn btn-sm" @click="xgCopy(h.v, 'Copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg> Copy</button></div></template>
    <div class="empty-state" x-show="!hist.length"><b>Nothing shared yet</b>Items you share appear here for quick reuse.</div>
  </section>
</div></div>
@endsection
