@php $xgTt = ($page ?? 'profiles') === 'timetable'; @endphp
@extends('layouts.app', ['activePage' => $xgTt ? 'timetable.html' : 'profiles.html'])
@section('title', $xgTt ? 'Timetable' : 'Profiles')
@section('content')
@php
  $daysMap = [1 => 'Mon', 2 => 'Tue', 3 => 'Wed', 4 => 'Thu', 5 => 'Fri', 6 => 'Sat', 7 => 'Sun'];
  $liveProfiles = isset($profiles) && $profiles->count() ? $profiles->values()->map(function ($pr) {
      $net = is_array($pr->network) ? $pr->network : [];
      $ag = is_array($pr->agent) ? $pr->agent : [];
      $coll = is_array($ag['collect'] ?? null) ? array_keys(array_filter($ag['collect'])) : [];
      $rules = is_array($ag['rule_actions'] ?? null) ? array_keys($ag['rule_actions']) : [];
      $tc = $ag['title_capture'] ?? 'app_only';
      return [
          'id' => $pr->id,
          'builtin' => (bool) $pr->builtin,
          'name' => $pr->name,
          'mode' => $pr->mode === 'always_on' ? 'always' : $pr->mode,
          'desc' => $pr->description ?? '',
          'block' => array_values($net['block_categories'] ?? []),
          'strict' => (bool) ($net['strict_egress'] ?? false),
          'aiov' => (bool) ($net['google_udm14'] ?? false),
          'collect' => $coll,
          'titles' => $tc === 'app_only' ? 'app' : $tc,
          'kill' => implode("\n", $rules),
      ];
  })->all() : [];

  $liveEntries = isset($entries) && $entries->count() ? $entries->values()->map(function ($e) use ($daysMap) {
      return [
          'id' => $e->id,
          'real' => true,
          'day' => $daysMap[$e->weekday] ?? 'Sun',
          'from' => substr((string) $e->start_time, 0, 5),
          'to' => substr((string) $e->end_time, 0, 5),
          'course' => $e->course,
          'profile' => $e->profile?->name ?? 'Unassigned',
          'profile_id' => $e->profile_id,
          'valid_from' => $e->valid_from?->format('Y-m-d') ?? '',
          'valid_until' => $e->valid_until?->format('Y-m-d') ?? '',
          'paused' => !$e->enabled,
      ];
  })->all() : [];
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'profiles' => $liveProfiles,
  'items' => $liveEntries,
  'formProfile' => $liveProfiles[0]['name'] ?? '',
  'canAdmin' => (bool) auth()->user()?->hasRole('admin'),
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="profilesPage"><div class="optbar">@if($xgTt)<span class="otitle">Timetable</span><span class="osub">When each class runs, and under which profile.</span>@else<span class="otitle">Profiles</span><span class="osub">What each class runs under.</span>@endif<span class="grow"></span></div><div class="canvas ">
  @if ($errors->any())
  <div class="note warn" style="margin-bottom:16px"><svg class="i i-sm" aria-hidden="true"><use href="#i-warn"/></svg><span>{{ $errors->first() }}</span></div>
  @endif
  @unless($xgTt)
  <!-- Profiles -->
  <div class="cols-side" style="grid-template-columns:300px minmax(0,1fr)">
    <div class="panel" style="min-height:100%">
      <div class="panel-head"><span class="h3">Profiles</span><button type="button" class="btn btn-sm" x-show="canAdmin" @click="newProfile()"><svg class="i i-sm" aria-hidden="true"><use href="#i-plus"/></svg> New</button></div>
      <div class="empty-state" x-show="!profiles.length"><b>No profiles yet</b>Click New to create one.</div>
      <ul class="plist" x-show="profiles.length"><template x-for="(pr, i) in profiles" :key="i"><li :class="cur === i && 'on'" @click="cur = i"><div class="grow" style="min-width:0"><div class="t" x-text="pr.name"></div><div class="d" x-text="pr.desc"></div></div><span class="mode" :class="pr.mode" x-text="pr.mode === 'always' ? 'Always' : pr.mode" style="text-transform:capitalize"></span></li></template></ul>
    </div>
    <div>
      <template x-if="p">
      <div>
      <form method="post" :action="p.id ? '/profiles/' + p.id : '/profiles'">
        @csrf
        <input type="hidden" name="mode" :value="p.mode === 'always' ? 'always_on' : p.mode">
        <input type="hidden" name="title_capture" :value="p.titles === 'app' ? 'app_only' : p.titles">
        <section class="panel panel-pad" style="padding:20px 24px">
          <div class="fgroup stack">
            <h2 class="h2">General</h2>
            <div class="form-grid"><label class="field"><span>Name</span><input name="name" class="input" x-model="p.name" :readonly="!canAdmin" required></label>
              <label class="field"><span>Type</span><select class="select" x-model="p.mode" :disabled="!canAdmin"><option value="always">Always-on</option><option value="practice">Practice</option><option value="test">Test</option><option value="exam">Exam</option></select></label></div>
            <label class="field"><span>Description</span><input name="description" class="input" x-model="p.desc" :readonly="!canAdmin"></label>
          </div>
          <div class="fgroup stack">
            <div><h2 class="h2">Blocking</h2><p class="muted small">Which kinds of sites are cut off during this profile.</p></div>
            <div class="checks-grid"><template x-for="c in cats" :key="c[0]"><label class="check"><input type="checkbox" name="block_categories[]" :value="c[0]" :checked="has(p.block, c[0])" :disabled="!canAdmin" @change="flip(p.block, c[0])"> <span x-text="c[1]"></span></label></template></div>
            <label class="check"><span class="switch"><input type="checkbox" name="strict_egress" value="1" x-model="p.strict" :disabled="!canAdmin"><i></i></span> Strict outbound: only approved destinations</label>
            <label class="check"><span class="switch"><input type="checkbox" name="google_udm14" value="1" x-model="p.aiov" :disabled="!canAdmin"><i></i></span> Hide AI summaries in search results</label>
          </div>
          <div class="fgroup stack">
            <div><h2 class="h2">What the agent notices</h2><p class="muted small">Keys pressed and pasted text are never collected.</p></div>
            <div class="checks-grid"><template x-for="c in coll" :key="c[0]"><label class="check"><input type="checkbox" name="collect[]" :value="c[0]" :checked="has(p.collect, c[0])" :disabled="!canAdmin" @change="flip(p.collect, c[0])"> <span x-text="c[1]"></span></label></template></div>
            <label class="field" style="max-width:260px"><span>Window titles</span><select class="select" x-model="p.titles" :disabled="!canAdmin"><option value="off">Not recorded</option><option value="app">Program name only</option><option value="full">Full title</option></select></label>
          </div>
          <div class="fgroup stack">
            <div><h2 class="h2">Close automatically</h2><p class="muted small">Detection rule IDs, one per line. Matching programs are closed on sight.</p></div>
            <textarea name="kill_rules" class="textarea mono" rows="3" x-model="p.kill" :readonly="!canAdmin" placeholder="proc.ollama"></textarea>
          </div>
        </section>
        <div class="savebar">
          <span class="muted small grow" x-text="canAdmin ? 'Changes apply to the next session that uses this profile.' : 'Administrator permission required to edit profiles.'"></span>
          <button type="button" class="btn btn-danger" x-show="canAdmin && p.id && !p.builtin" @click="deleteCurrent()">Delete profile</button>
          <button type="button" class="btn" x-show="canAdmin" @click="reload()">Discard</button>
          <button type="submit" class="btn btn-solid" x-show="canAdmin">Save profile</button>
        </div>
      </form>
      <form id="xg-del-profile-form" method="post" :action="'/profiles/' + p.id" style="display:none">@csrf @method('DELETE')</form>
      </div>
      </template>
      <div class="panel panel-pad empty-state" x-show="!p"><b>No profile selected</b>Create a profile to configure policy rules.</div>
    </div>
  </div>

  @endunless

  @if($xgTt)
  <!-- Timetable -->
  <div class="cols-main" style="grid-template-columns:minmax(0,1fr) 380px">
    <section class="panel panel-pad">
      <div class="row" style="margin-bottom:14px"><h2 class="h2 grow">This week</h2><button class="btn btn-solid btn-sm" @click="addOpen = !addOpen"><svg class="i i-sm" aria-hidden="true"><use href="#i-plus"/></svg> Add a class</button></div>
      <div class="week" :style="'--hr:' + hr">
        <div class="hd"></div><template x-for="d in days" :key="'h' + d"><div class="hd" x-text="d"></div></template>
        <div class="hours" :style="'height:' + ((end - start) * hr) + 'px'"><template x-for="h in (end - start)" :key="'t' + h"><span :style="'top:' + ((h - 1) * hr) + 'px'" x-text="String(start + h - 1).padStart(2, '0')"></span></template></div>
        <template x-for="d in days" :key="'c' + d"><div class="daycol" :style="'height:' + ((end - start) * hr) + 'px'"><template x-for="i in items.filter(x => x.day === d)" :key="i.id"><div class="slot" :class="['m-' + modeOf(i.profile), i.paused ? 'paused' : '']" :style="'top:' + top(i.from) + 'px;height:' + tall(i) + 'px'"><b x-text="i.course"></b><span x-text="i.from + '–' + i.to"></span></div></template></div></template>
      </div>
    </section>
    <section class="panel">
      <div class="panel-head"><h2 class="h2">Schedule</h2></div>
      <template x-for="i in items" :key="i.id"><div class="row" style="padding:12px 20px;border-bottom:1px solid var(--border);flex-wrap:nowrap" :style="i.paused && 'opacity:.55'"><div class="grow"><div style="font-weight:500" x-text="i.course"></div><div class="small muted" x-text="i.day + ' ' + i.from + '–' + i.to + ' · ' + i.profile + (i.valid_from || i.valid_until ? ' (' + (i.valid_from || '…') + ' → ' + (i.valid_until || '…') + ')' : '')"></div></div><button class="btn btn-sm btn-quiet" @click="toggleSlot(i)" x-text="i.paused ? 'Resume' : 'Pause'"></button><button class="btn btn-sm btn-quiet" @click="removeSlot(i)" aria-label="Remove"><svg class="i i-sm" aria-hidden="true"><use href="#i-trash"/></svg></button></div></template>
      <div class="empty-state" x-show="!items.length"><b>No classes yet</b>Add one to start sessions automatically.</div>
      <form x-show="addOpen" x-cloak method="post" action="{{ route('timetable.store') }}" class="panel-body stack" style="background:var(--surface-2);border-top:1px solid var(--border)">
        @csrf
        <input type="hidden" name="weekday" :value="dayNum(form.day)">
        <div class="form-grid"><label class="field"><span>Day</span><select class="select" x-model="form.day"><template x-for="d in days"><option x-text="d"></option></template></select></label><label class="field"><span>Course</span><input name="course" class="input" x-model="form.course" required></label>
        <label class="field"><span>Starts</span><input name="start_time" class="input" type="time" x-model="form.from"></label><label class="field"><span>Ends</span><input name="end_time" class="input" type="time" x-model="form.to"></label></div>
        <label class="field"><span>Profile</span>
          <select name="profile_id" class="select" required @change="form.profile = $event.target.options[$event.target.selectedIndex].text">
            @foreach($profiles ?? [] as $prof)
              <option value="{{ $prof->id }}">{{ $prof->name }}</option>
            @endforeach
          </select>
        </label>
        <div class="form-grid">
          <label class="field"><span>Valid from (optional)</span><input name="valid_from" class="input" type="date" x-model="form.valid_from"></label>
          <label class="field"><span>Valid until (optional)</span><input name="valid_until" class="input" type="date" x-model="form.valid_until"></label>
        </div>
        <div class="row"><button type="submit" class="btn btn-solid">Add</button><button type="button" class="btn btn-quiet" @click="addOpen = false">Cancel</button></div>
      </form>
    </section>
  </div>
  @endif
</div></div>
@endsection
