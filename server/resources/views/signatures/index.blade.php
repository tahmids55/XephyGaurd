@extends('layouts.app')
@section('title', 'Blocklists & signatures')
@section('content')
<div class="grid gap-4 xl:grid-cols-2">
  <div class="card">
    <h2 class="mb-2 text-lg font-semibold">Network blocklists</h2>
    <div class="mb-3 flex flex-wrap gap-2 text-sm">
      <a href="{{ route('signatures.index') }}" @class(['badge', 'bg-indigo-600 text-white' => ! $category, 'sev-info' => $category])>all</a>
      @foreach (\App\Models\BlocklistEntry::CATEGORIES as $c)
        <a href="?category={{ $c }}" @class(['badge', 'bg-indigo-600 text-white' => $category === $c, 'sev-info' => $category !== $c])>{{ $c }} ({{ $counts[$c] ?? 0 }})</a>@endforeach
    </div>
    <form method="post" action="{{ route('blocklist.add') }}" class="mb-2 flex gap-2">@csrf
      <select name="category" class="input w-36">@foreach (\App\Models\BlocklistEntry::CATEGORIES as $c)<option @selected($category === $c)>{{ $c }}</option>@endforeach</select>
      <input name="value" class="input" placeholder="domain.com or host.com/path" required><button class="btn btn-primary">Add</button></form>
    <form method="post" action="{{ route('blocklist.import') }}" enctype="multipart/form-data" class="mb-3 flex gap-2">@csrf
      <select name="category" class="input w-36">@foreach (\App\Models\BlocklistEntry::CATEGORIES as $c)<option>{{ $c }}</option>@endforeach</select>
      <input type="file" name="file" class="input" required><input name="source" class="input w-32" placeholder="list name"><button class="btn">Import</button></form>
    <form class="mb-2"><input type="hidden" name="category" value="{{ $category }}"><input name="q" class="input" placeholder="Search…" value="{{ request('q') }}"></form>
    <table class="tbl"><tr><th>Category</th><th>Entry</th><th>Kind</th><th>Source</th><th></th></tr>
      @foreach ($entries as $e)
        <tr @class(['opacity-40' => ! $e->enabled])><td>{{ $e->category }}</td><td class="font-mono text-xs">{{ $e->value }}</td><td class="text-xs">{{ $e->kind }}</td><td class="text-xs">{{ $e->source }}</td>
          <td class="flex gap-1"><form method="post" action="{{ route('blocklist.toggle', $e) }}">@csrf<button class="btn btn-sm">{{ $e->enabled ? 'Disable' : 'Enable' }}</button></form>
            <form method="post" action="{{ route('blocklist.delete', $e) }}">@csrf @method('delete')<button class="btn btn-sm">✕</button></form></td></tr>
      @endforeach
    </table>{{ $entries->links() }}
    <p class="mt-2 text-xs text-slate-500">Changes reach DNS and the proxy within seconds (hot reload). Path rules apply only on inspected hosts.</p>
  </div>
  <div class="space-y-4">
    <div class="card">
      <div class="flex items-center justify-between"><h2 class="text-lg font-semibold">Agent signatures</h2>
        <form method="post" action="{{ route('signatures.publish') }}" class="flex gap-2">@csrf<input name="note" class="input w-48" placeholder="change note"><button class="btn btn-primary">Publish to agents</button></form></div>
      <table class="tbl mt-2"><tr><th>Version</th><th>Note</th><th>Created</th><th></th></tr>
        @foreach ($versions as $v)<tr><td class="font-mono text-xs">{{ $v->version }}</td><td>{{ $v->note }}</td><td class="text-xs">{{ $v->created_at->diffForHumans() }}</td>
          <td>@if ($v->active)<span class="badge bg-emerald-100 text-emerald-800">active</span>@else<form method="post" action="{{ route('signatures.activate', $v) }}">@csrf<button class="btn btn-sm">Roll back</button></form>@endif</td></tr>@endforeach
      </table>
    </div>
    <div class="card">
      <h2 class="mb-2 text-lg font-semibold">Rules ({{ $rules->count() }})</h2>
      <div class="max-h-[50vh] overflow-y-auto"><table class="tbl"><tr><th>Rule</th><th>Kind</th><th>Severity</th><th>Description</th><th></th></tr>
        @foreach ($rules as $r)<tr @class(['opacity-40' => ! $r->enabled])><td class="font-mono text-xs">{{ $r->rule_id }}</td><td class="text-xs">{{ $r->kind }}</td>
          <td><span class="badge sev-{{ $r->severity }}">{{ $r->severity }}</span></td><td class="text-xs">{{ $r->description }}</td>
          <td><form method="post" action="{{ route('rules.toggle', $r) }}">@csrf<button class="btn btn-sm">{{ $r->enabled ? 'Disable' : 'Enable' }}</button></form></td></tr>@endforeach
      </table></div>
      <details class="mt-3"><summary class="cursor-pointer text-sm font-semibold">Add / edit a rule</summary>
        <form method="post" action="{{ route('rules.save') }}" class="mt-2 grid grid-cols-3 gap-2 text-sm">@csrf
          <input name="rule_id" class="input" placeholder="proc.myapp" required>
          <select name="kind" class="input">@foreach (['process', 'extension', 'port', 'artifact', 'adapter', 'device'] as $k)<option>{{ $k }}</option>@endforeach</select>
          <select name="severity" class="input">@foreach (['low', 'medium', 'high', 'critical'] as $s)<option>{{ $s }}</option>@endforeach</select>
          <input name="description" class="input col-span-3" placeholder="Description">
          <textarea name="definition" rows="4" class="input col-span-3 font-mono text-xs" placeholder='{"match": {"exe": ["myapp"]}}' required></textarea>
          <div class="col-span-3">Test-mode actions: @foreach (['log', 'warn', 'kill', 'alert', 'isolate'] as $a)<label class="mr-2"><input type="checkbox" name="test_actions[]" value="{{ $a }}"> {{ $a }}</label>@endforeach</div>
          <button class="btn btn-primary">Save rule</button></form></details>
    </div>
  </div>
</div>
@endsection
