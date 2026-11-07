@extends('layouts.app')
@section('title', 'Add a PC')
@section('content')
<div class="grid gap-4 lg:grid-cols-2">
  <div class="card space-y-3">
    <h2 class="text-lg font-semibold">Add a PC</h2>
    <div><div class="label">Lab enrollment key</div>
      <div class="flex items-center gap-2"><code class="rounded bg-slate-100 px-3 py-2 font-mono text-lg">{{ $key ?? '—' }}</code>
        @if (auth()->user()->hasRole('admin'))<form method="post" action="{{ route('enroll.rotate') }}" onsubmit="return confirm('The old key stops working. Continue?')">@csrf<button class="btn btn-sm">Rotate</button></form>@endif</div></div>
    <div><div class="label">CA fingerprint (SHA-256)</div><code class="text-xs break-all">{{ $fingerprint ?? 'CA not initialised' }}</code></div>
    <div>
      <div class="label">Linux (Ubuntu/Debian) — one line, as root</div>
      <pre class="overflow-x-auto rounded bg-slate-900 p-3 text-xs text-slate-100">curl -fsSL http://{{ $ccc }}/install/linux.sh | sudo bash -s -- --key {{ $key }}</pre>
      <p class="text-xs text-slate-500">Many PCs: use the Ansible role in installers/ansible (one inventory file).</p>
    </div>
    <div>
      <div class="label">Windows — silent install (GPO, Intune, PDQ, imaging)</div>
      <pre class="overflow-x-auto rounded bg-slate-900 p-3 text-xs text-slate-100">XephyGuard-Setup.exe /S /KEY={{ $key }} /CCC={{ $ccc }} /CAFP={{ $fingerprint }}</pre>
    </div>
    <div><div class="label">Downloads</div>
      @forelse ($downloads as $d)<a class="mr-3 text-indigo-600 underline" href="{{ route('downloads', $d) }}">{{ $d }}</a>@empty
        <p class="text-xs text-slate-500">No installers uploaded yet. Copy the release artifacts into <code>storage/app/public/downloads/</code> (scripts/release.sh does this).</p>@endforelse
      <a class="ml-1 text-indigo-600 underline" href="{{ route('ca.cert') }}">xephyguard-ca.crt</a></div>
    <p class="text-sm text-slate-600">New PCs find this CCC automatically and appear below as <b>Pending</b>. Click <b>Approve</b> — nothing else to configure.</p>
    @if (isset($issuedKey) && $issuedKey)
      <div class="rounded border border-emerald-300 bg-emerald-50 p-3 text-xs text-emerald-900">
        <div class="font-semibold">New short-lived enrollment key (copy now — shown once):</div>
        <code class="mt-1 block font-mono text-base font-bold">{{ $issuedKey }}</code>
      </div>
    @endif
    @if (auth()->user()->hasRole('admin'))
      <div class="border-t border-slate-200 pt-3 space-y-2">
        <div class="label">Short-lived revocable enrollment keys</div>
        <form method="post" action="{{ route('enroll.keys.create') }}" class="grid grid-cols-1 gap-2 sm:grid-cols-4">@csrf
          <input name="label" class="input sm:col-span-2" placeholder="Label (e.g. Lab 2 Row B)" required>
          <input name="ttl_hours" type="number" min="1" max="8760" value="24" class="input" placeholder="TTL (hours)">
          <input name="max_uses" type="number" min="1" max="10000" class="input" placeholder="Max uses (opt)">
          <input name="allowed_macs" class="input sm:col-span-3 font-mono text-xs" placeholder="Optional allowed MACs (comma-separated)">
          <button class="btn btn-primary btn-sm">Issue key</button>
        </form>
        @if (isset($enrollmentKeys) && $enrollmentKeys->isNotEmpty())
          <table class="tbl text-xs">
            <tr><th>Prefix</th><th>Label</th><th>Uses</th><th>Expires</th><th>Status</th><th></th></tr>
            @foreach ($enrollmentKeys as $ek)
              <tr>
                <td class="font-mono">{{ $ek->key_prefix }}…</td>
                <td>{{ $ek->label }}</td>
                <td>{{ $ek->uses_count }}{{ $ek->max_uses ? '/'.$ek->max_uses : '' }}</td>
                <td>{{ $ek->expires_at?->diffForHumans() ?? 'never' }}</td>
                <td>
                  @if ($ek->revoked_at)
                    <span class="badge bg-red-100 text-red-700">revoked</span>
                  @elseif ($ek->expires_at && $ek->expires_at->isPast())
                    <span class="badge bg-slate-200 text-slate-600">expired</span>
                  @elseif ($ek->max_uses && $ek->uses_count >= $ek->max_uses)
                    <span class="badge bg-amber-100 text-amber-800">exhausted</span>
                  @else
                    <span class="badge bg-emerald-100 text-emerald-800">active</span>
                  @endif
                </td>
                <td>
                  @if (! $ek->revoked_at)
                    <form method="post" action="{{ route('enroll.keys.revoke', $ek) }}">@csrf<button class="text-xs text-red-600">revoke</button></form>
                  @endif
                </td>
              </tr>
            @endforeach
          </table>
        @endif
      </div>
    @endif
  </div>
  <div class="card">
    <div class="mb-2 flex items-center justify-between"><h2 class="text-lg font-semibold">Pending approval ({{ $pending->count() }})</h2>
      @if ($pending->count() > 1)<form method="post" action="{{ route('enroll.approve_all') }}">@csrf<button class="btn btn-primary btn-sm">Approve all</button></form>@endif</div>
    <table class="tbl"><tr><th>Hostname</th><th>OS</th><th>IP / MAC</th><th>Name</th><th></th></tr>
      @forelse ($pending as $e)
        <tr><td class="font-semibold">{{ $e->hostname }}</td><td class="text-xs">{{ $e->os }} {{ $e->os_version }}</td>
          <td class="text-xs">{{ $e->ip }}<br>{{ implode(', ', $e->macs ?? []) }}</td>
          <td colspan="2"><form method="post" action="{{ route('enroll.approve', $e) }}" class="flex gap-1">@csrf
              <input name="name" class="input w-28" placeholder="{{ $e->hostname }}"><button class="btn btn-primary btn-sm">Approve</button></form>
            <form method="post" action="{{ route('enroll.reject', $e) }}" class="mt-1">@csrf<button class="text-xs text-red-600">reject</button></form></td></tr>
      @empty<tr><td colspan="5" class="py-6 text-center text-slate-500">Nothing pending.</td></tr>@endforelse
    </table>
    @if (auth()->user()->hasRole('admin'))
      <form method="post" action="{{ route('enroll.auto') }}" class="mt-4 space-y-2 border-t border-slate-200 pt-3">@csrf
        <label class="flex items-center gap-2 text-sm font-semibold"><input type="checkbox" name="auto_approve" value="1" @checked($lab->auto_approve)> Auto-approve PCs whose MAC is on this list</label>
        <textarea name="macs" rows="3" class="input font-mono text-xs" placeholder="aa:bb:cc:dd:ee:ff, one per line">{{ implode("\n", $lab->auto_approve_macs ?? []) }}</textarea>
        <button class="btn btn-sm">Save</button></form>
    @endif
  </div>
  <div class="card lg:col-span-2">
    <h2 class="mb-2 text-lg font-semibold">Enrolled PCs ({{ $pcs->whereNull('retired_at')->count() }})</h2>
    <table class="tbl"><tr><th>Name</th><th>Hostname</th><th>OS</th><th>IP</th><th>Agent</th><th>Last seen</th><th>Cert expires</th><th></th></tr>
      @foreach ($pcs as $pc)
        <tr @class(['opacity-50' => $pc->retired_at])>
          <td><form method="post" action="{{ route('pcs.rename', $pc) }}" class="flex gap-1">@csrf<input name="name" value="{{ $pc->name }}" class="input w-32"><button class="btn btn-sm">✓</button></form></td>
          <td>{{ $pc->hostname }}</td><td class="text-xs">{{ $pc->os }}</td><td>{{ $pc->ip }}</td><td>{{ $pc->agent?->version }}</td>
          <td class="text-xs">{{ $pc->last_seen_at?->diffForHumans() ?? 'never' }}</td><td class="text-xs">{{ $pc->agent?->cert_expires_at?->format('Y-m-d') }}</td>
          <td>@if ($pc->retired_at)<span class="text-xs">retired</span>@elseif (auth()->user()->hasRole('admin'))
            <form method="post" action="{{ route('pcs.retire', $pc) }}" onsubmit="return confirm('Retire {{ $pc->name }}? Its credentials are revoked.')">@csrf<button class="text-xs text-red-600">retire</button></form>@endif</td></tr>
      @endforeach
    </table>
  </div>
</div>
@endsection
