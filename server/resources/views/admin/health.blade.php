@extends('layouts.app')
@section('title', 'System health')
@section('content')
<div class="grid gap-4 lg:grid-cols-3">
  <div class="card lg:col-span-2">
    <h2 class="mb-2 text-lg font-semibold">Doctor</h2>
    <table class="tbl">
      @foreach ($checks as $c)
        <tr><td class="w-8 text-lg">{!! $c['ok'] ? '<span class="text-emerald-600">✔</span>' : ($c['level'] === 'error' ? '<span class="text-red-600">✘</span>' : '<span class="text-amber-500">!</span>') !!}</td>
          <td class="font-semibold">{{ $c['check'] }}</td><td>{{ $c['detail'] }}@if (! $c['ok'] && $c['fix'])<div class="text-xs text-slate-500">→ {{ $c['fix'] }}</div>@endif</td></tr>
      @endforeach
    </table>
    <p class="mt-2 text-xs text-slate-500">Host checks (IP forwarding, nftables, time sync): run <code>sudo xephyguard-ccc doctor</code> on the CCC.</p>
  </div>
  <div class="space-y-4">
    <div class="card"><h2 class="mb-2 font-semibold">Agent versions</h2>
      @foreach ($versions as $v => $n)<div class="flex justify-between text-sm"><span class="font-mono">{{ $v }}</span><span>{{ $n }} PCs</span></div>@endforeach</div>
    <div class="card"><h2 class="mb-2 font-semibold">Disk</h2>
      <div class="text-sm">{{ $disk['free'] ? round($disk['free'] / 1e9, 1) : '?' }} GB free of {{ $disk['total'] ? round($disk['total'] / 1e9, 1) : '?' }} GB</div></div>
    <div class="card"><h2 class="mb-2 font-semibold">Needs attention</h2>
      @forelse ($attention as $pc)
        <div class="border-b border-slate-100 py-1 text-sm"><b>{{ $pc->name }}</b>:
          @if (! $pc->online()) offline since {{ $pc->last_seen_at?->diffForHumans() ?? 'never' }} @endif
          @if ($pc->isolated) isolated @endif
          @if (($pc->usage['helpers'] ?? 1) === 0) helper not running @endif
          @if (($pc->capabilities['keys'] ?? true) === false) cannot see key timing @endif</div>
      @empty <p class="text-sm text-slate-500">All good.</p> @endforelse</div>
  </div>
</div>
@endsection
