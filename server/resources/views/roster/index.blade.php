@extends('layouts.app', ['activePage' => 'roster.html'])
@section('title', 'Roster')
@section('content')
@php
  $liveRows = isset($students) && $students->count() ? $students->getCollection()->map(function ($s) {
      return [
          'dbId' => $s->id,
          'id' => $s->student_number,
          'name' => $s->name,
          'login' => $s->login,
          'token' => substr((string) ($s->pseudonym ?? hash('sha256', (string) $s->student_number)), 0, 12),
          'consent' => $s->latestConsent ? ($s->latestConsent->granted ? 'granted' : 'declined') : 'declined',
          'ex' => $s->exempt ? ($s->exempt_reason ?? 'Accommodation') : '',
          'exOpen' => false,
      ];
  })->values()->all() : [];
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'pseudo' => (bool) ($pseudonymize ?? true),
  'rows' => $liveRows,
  'total' => isset($students) ? $students->total() : 0,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="rosterPage">
  <form id="xg-roster-import" method="post" action="{{ route('roster.import') }}" enctype="multipart/form-data" style="display:none">
    @csrf
    <input x-ref="csv" id="csv" name="file" type="file" accept=".csv" hidden @change="submitImport($event)">
  </form>
  <div class="optbar"><span class="otitle">Roster</span><span class="osub">Maps lab logins to students. Exports use the pseudonym only.</span><span class="grow"></span><button type="button" class="btn btn-solid" @click="triggerImport()"><svg class="i i-sm" aria-hidden="true"><use href="#i-upload"/></svg> Import CSV</button></div><div class="canvas ">
  
  <div class="toolbar"><input class="input input-sm" style="width:280px" placeholder="Search ID, name or login" x-model="q"><span class="grow"></span><label class="check small"><span class="switch"><input type="checkbox" x-model="pseudo" @change="togglePseudo()"><i></i></span> Show pseudonyms</label></div>
  <section class="panel">
    <div class="table-wrap"><table class="tbl">
      <thead><tr><th>ID</th><th>Name</th><th>Login</th><th>Pseudonym</th><th>Research consent</th><th>Accommodation</th></tr></thead>
      <tbody><template x-for="r in shown" :key="r.id"><tr>
        <td class="mono small" x-text="r.id"></td><td class="name" x-text="r.name"></td><td class="mono small" x-text="r.login"></td><td class="mono small muted" x-text="pseudo ? r.token : 'Not used'"></td>
        <td><div class="seg"><button :aria-pressed="r.consent === 'granted'" @click="setConsent(r, 'granted')">Yes</button><button :aria-pressed="r.consent === 'declined'" @click="setConsent(r, 'declined')">No</button></div></td>
        <td>
          <template x-if="r.ex && !r.exOpen"><span class="row"><span class="chip info" x-text="r.ex"></span><button class="btn btn-sm btn-quiet" @click="saveExempt(r, false)">Remove</button></span></template>
          <template x-if="!r.ex && !r.exOpen"><button class="link-btn" @click="r.exOpen = true">Add</button></template>
          <template x-if="r.exOpen"><span class="row"><input class="input input-sm" style="width:200px" placeholder="What does the student need?" x-model="r.ex" @keydown.enter="saveExempt(r, true)"><button class="btn btn-sm" @click="saveExempt(r, true)">Save</button></span></template>
        </td></tr></template>
        <template x-if="!shown.length"><tr><td colspan="6" class="muted" style="padding:18px 16px">No students in roster yet. Import a CSV to map lab logins.</td></tr></template>
      </tbody>
    </table></div>
    <div class="tbl-foot">
      <span x-text="'Showing ' + shown.length + ' of ' + total + ' students'"></span>
      @if(isset($students) && $students->hasPages())
        <div class="pager">
          @if($students->onFirstPage())
            <button disabled>Prev</button>
          @else
            <a class="btn btn-sm" href="{{ $students->previousPageUrl() }}">Prev</a>
          @endif
          @if($students->hasMorePages())
            <a class="btn btn-sm" href="{{ $students->nextPageUrl() }}">Next</a>
          @else
            <button disabled>Next</button>
          @endif
        </div>
      @endif
    </div>
  </section>
</div></div>
@endsection
