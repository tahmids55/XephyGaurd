@extends('layouts.app', ['activePage' => 'enroll.html'])
@section('title', 'Add a PC')
@section('content')
@php
  $cccHost = $ccc ?: request()->getHost();
  $portSuffix = request()->getPort() && !in_array(request()->getPort(), [80, 443], true) ? ':' . request()->getPort() : '';
  $baseUrl = (request()->isSecure() ? 'https://' : 'http://') . $cccHost . $portSuffix;
  $exeName = $setupFile ?? '';

  $livePending = isset($pending) && $pending->count() ? $pending->map(function ($e) {
      return [
          'id' => $e->id,
          'real' => true,
          'host' => $e->hostname,
          'os' => trim($e->os . ' ' . ($e->os_version ?? '')),
          'ip' => $e->ip ?? '—',
          'mac' => collect($e->macs ?? [])->first() ?? '—',
          'name' => $e->hostname,
          'conflict' => $e->conflictPc ? ['id' => $e->conflictPc->id, 'name' => $e->conflictPc->name] : null,
      ];
  })->values()->all() : [];

  $livePcs = isset($pcs) && $pcs->count() ? $pcs->map(function ($p) {
      return [
          'id' => $p->id,
          'real' => true,
          'name' => $p->name,
          'host' => $p->hostname,
          'os' => trim($p->os . ' ' . ($p->os_version ?? '')),
          'ip' => $p->ip ?? '—',
          'agent' => $p->agent?->version ?? '—',
          'status' => $p->online() ? 'normal' : 'offline',
          'lastSeen' => $p->last_seen_at ? $p->last_seen_at->diffForHumans() : 'Never',
          'certExpires' => $p->agent?->cert_expires_at ? $p->agent->cert_expires_at->format('Y-m-d') : '—',
          'retired' => $p->retired_at !== null,
          'editingName' => false,
      ];
  })->values()->all() : [];
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'key' => $key ?? '',
  'fp' => $fingerprint ?? '',
  'exeName' => $exeName,
  'cccHost' => $cccHost,
  'baseUrl' => $baseUrl,
  'pending' => $livePending,
  'enrolled' => $livePcs,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="enrollPage"><div class="optbar"><span class="otitle">Add a PC</span><span class="osub">Install the agent, then approve the PC when it appears.</span><span class="grow"></span><a class="btn" href="{{ route('downloads.index') }}"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Downloads</a></div><div class="canvas ">
  

  <div class="cols-2">
    <section class="panel">
      <div class="panel-head"><h2 class="h2">1. Install the agent</h2></div>
      <div class="panel-body stack">
        <div><div class="cmd-label">Lab enrolment key</div>
          <div class="key">
            <code x-text="key || 'Not set'"></code>
            <button type="button" class="btn btn-sm" x-show="key" @click="xgCopy(key, 'Key copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg> Copy</button>
            <form method="post" action="{{ route('enroll.rotate') }}" style="display:inline" @submit="confirmSubmit($event, 'The old key stops working for new installs. Continue?')">
              @csrf
              <button type="submit" class="btn btn-sm btn-quiet">Rotate</button>
            </form>
          </div>
        </div>
        <div><div class="cmd-label">One-time PC token (auto-approves one PC; the lab key alone waits for approval)</div>
          @if (session('enroll_token'))
          <div class="key"><code>{{ session('enroll_token') }}</code><button type="button" class="btn btn-sm" @click="xgCopy(@js(session('enroll_token')), 'Token copied')">Copy</button></div>
          <p class="help">Shown once. Pass it to the installer as the enrollment token.</p>
          @endif
          <form method="post" action="{{ route('enroll.token') }}" class="row" style="--gap:6px">
            @csrf
            <input class="input input-sm" name="pc_name" maxlength="64" placeholder="PC name (optional)" style="width:180px">
            <select class="input input-sm" name="hours"><option value="24">1 day</option><option value="72" selected>3 days</option><option value="168">7 days</option></select>
            <button type="submit" class="btn btn-sm">Create token</button>
          </form>
          @if ($tokens->count())
          <ul class="small muted" style="margin-top:6px">
            @foreach ($tokens as $t)
            <li>{{ $t->pc_name ?: 'any PC' }}, expires {{ $t->expires_at->format('Y-m-d H:i') }}, by {{ $t->creator?->name ?? '—' }}
              <form method="post" action="{{ route('enroll.token_revoke', $t) }}" style="display:inline">@csrf<button type="submit" class="btn btn-sm btn-quiet">Revoke</button></form></li>
            @endforeach
          </ul>
          @endif
        </div>
        <div>
          <div class="seg" style="margin-bottom:10px"><button :aria-pressed="os === 'win'" @click="os = 'win'">Windows</button><button :aria-pressed="os === 'linux'" @click="os = 'linux'">Ubuntu / Debian</button></div>
          <div class="cmd"><pre x-text="cmd"></pre><button class="btn btn-sm copy" @click="xgCopy(cmd, 'Command copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg></button></div>
          <p class="help" x-show="os === 'win'">Run as administrator. Works from Group Policy or a disk image.</p>
          <p class="help" x-show="os === 'linux'" x-cloak>For a whole lab, use the Ansible role in <code>installers/ansible</code>.</p>
        </div>
      </div>
    </section>

    <section class="panel">
      <div class="panel-head">
        <h2 class="h2">2. Approve new PCs</h2>
        <span class="chip alert" x-show="pending.length" x-text="pending.length + ' waiting'"></span>
        <form method="post" action="{{ route('enroll.approve_all') }}" x-show="pending.length" style="display:inline">
          @csrf
          <button type="submit" class="btn btn-sm">Approve all</button>
        </form>
      </div>
      <template x-for="p in pending" :key="p.id">
        <div class="row" style="padding:14px 20px;border-bottom:1px solid var(--border);flex-wrap:nowrap;--gap:14px">
          <div class="grow" style="min-width:0"><div class="mono" style="font-weight:500" x-text="p.host"></div><div class="small muted" x-text="p.os + ' · ' + p.ip"></div><div class="small" x-show="p.conflict" x-text="p.conflict ? 'Looks like ' + p.conflict.name + ' (same hostname or MAC, different machine ID). Approve adds a new PC unless you adopt it.' : ''"></div></div>
          <input class="input input-sm" style="width:110px" x-model="p.name" aria-label="Display name">
          <button class="btn btn-sm btn-solid" @click="approve(p)">Approve</button><button class="btn btn-sm" x-show="p.conflict" @click="approve(p, p.conflict.id)" x-text="p.conflict ? 'Adopt ' + p.conflict.name : ''"></button><button class="btn btn-sm btn-quiet" @click="reject(p)">Reject</button>
        </div>
      </template>
      <div class="empty-state" x-show="!pending.length"><b>No PCs waiting</b>New installs appear here.</div>
    </section>
  </div>

  <section class="panel">
    <div class="panel-head"><h2 class="h2">Enrolled PCs</h2><span class="muted small" x-text="enrolled.length + ' total'"></span><input class="input input-sm" style="width:220px" placeholder="Search" x-model="q"></div>
    <div class="table-wrap"><table class="tbl">
      <thead><tr><th>PC</th><th>Computer</th><th>System</th><th>Address</th><th>Agent</th><th>Last seen</th><th>Cert expires</th><th></th></tr></thead>
      <tbody><template x-for="p in shown" :key="p.id"><tr :style="p.retired && 'opacity:.5'">
        <td class="name">
          <span class="dot" :class="p.status === 'offline' ? 'off' : 'ok'" style="margin-right:10px"></span>
          <template x-if="!p.editingName">
            <span><span x-text="p.name"></span> <button type="button" class="link-btn small muted" style="margin-left:6px" @click="p.editingName = true">Rename</button></span>
          </template>
          <template x-if="p.editingName">
            <span class="row" style="display:inline-flex;--gap:6px"><input class="input input-sm" style="width:110px" x-model="p.name" @keydown.enter="renamePc(p)"><button type="button" class="btn btn-sm" @click="renamePc(p)">Save</button></span>
          </template>
        </td>
        <td class="mono small" x-text="p.host"></td><td x-text="p.os"></td><td class="mono small" x-text="p.ip"></td>
        <td><span class="mono small" x-text="p.agent && p.agent !== '—' ? 'v' + p.agent : '—'"></span></td>
        <td class="small muted" x-text="p.lastSeen || 'Never'"></td>
        <td class="mono small muted" x-text="p.certExpires || '—'"></td>
        <td class="right"><button class="btn btn-sm btn-quiet" x-show="!p.retired" @click="retirePc(p)">Retire</button><span class="small muted" x-show="p.retired">Retired</span></td></tr></template>
        <template x-if="!shown.length"><tr><td colspan="8" class="muted" style="padding:18px 16px">No enrolled PCs match this filter.</td></tr></template>
      </tbody>
    </table></div>
  </section>
</div></div>
@endsection
