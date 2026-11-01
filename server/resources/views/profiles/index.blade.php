@extends('layouts.app')
@section('title', 'Timetable & profiles')
@section('content')
@php($days = [1 => 'Mon', 2 => 'Tue', 3 => 'Wed', 4 => 'Thu', 5 => 'Fri', 6 => 'Sat', 7 => 'Sun'])
@php($isAdmin = auth()->user()->hasRole('admin'))
<div class="grid gap-4 xl:grid-cols-2">
  <div class="card">
    <h2 class="mb-2 text-lg font-semibold">Weekly timetable</h2>
    <p class="mb-3 text-sm text-slate-500">Sessions start and end automatically. Blocking relaxes when a session ends.</p>
    <table class="tbl"><tr><th>Day</th><th>Time</th><th>Course</th><th>Profile</th><th>Valid</th><th></th></tr>
      @foreach ($entries as $e)
        <tr @class(['opacity-50' => ! $e->enabled])><td>{{ $days[$e->weekday] }}</td><td>{{ substr($e->start_time, 0, 5) }}–{{ substr($e->end_time, 0, 5) }}</td>
          <td>{{ $e->course }}</td><td>{{ $e->profile->name }}</td><td class="text-xs">{{ $e->valid_from?->format('M d') }} – {{ $e->valid_until?->format('M d') }}</td>
          <td class="flex gap-1"><form method="post" action="{{ route('timetable.toggle', $e) }}">@csrf<button class="btn btn-sm">{{ $e->enabled ? 'Pause' : 'Enable' }}</button></form>
            <form method="post" action="{{ route('timetable.delete', $e) }}">@csrf @method('delete')<button class="btn btn-sm">✕</button></form></td></tr>
      @endforeach
    </table>
    <form method="post" action="{{ route('timetable.store') }}" class="mt-3 grid grid-cols-6 gap-2">@csrf
      <select name="weekday" class="input">@foreach ($days as $n => $d)<option value="{{ $n }}">{{ $d }}</option>@endforeach</select>
      <input type="time" name="start_time" class="input" required><input type="time" name="end_time" class="input" required>
      <input name="course" class="input" placeholder="Course" required>
      <select name="profile_id" class="input">@foreach ($profiles as $p)<option value="{{ $p->id }}">{{ $p->name }}</option>@endforeach</select>
      <button class="btn btn-primary">Add</button>
      <input type="date" name="valid_from" class="input col-span-3" title="valid from (optional)"><input type="date" name="valid_until" class="input col-span-3" title="valid until (optional)">
    </form>
  </div>
  <div class="card space-y-4">
    <h2 class="text-lg font-semibold">Profiles</h2>
    @foreach ($profiles->push(new \App\Models\Profile(['mode' => 'practice', 'network' => [], 'agent' => ['title_capture' => 'app_only', 'collect' => ['window' => true, 'clipboard' => true, 'keys' => true]]])) as $p)
      <details @class(['rounded-lg border border-slate-200 p-3', 'bg-slate-50' => ! $p->exists]) @if (! $p->exists) open @endif>
        <summary class="cursor-pointer"><span class="badge mode-{{ $p->mode }}">{{ $p->mode }}</span> <b>{{ $p->exists ? $p->name : 'New profile' }}</b>
          @if ($p->builtin)<span class="text-xs text-slate-400">built-in</span>@endif <span class="text-sm text-slate-500">{{ $p->description }}</span></summary>
        <form method="post" action="{{ route('profiles.save', $p->exists ? $p : null) }}" class="mt-3 grid grid-cols-2 gap-3 text-sm">@csrf
          <fieldset @disabled(! $isAdmin) class="contents">
          <div><label class="label">Name</label><input name="name" class="input" value="{{ $p->name }}" required></div>
          <div><label class="label">Mode</label><select name="mode" class="input">@foreach (\App\Models\Profile::MODES as $m)<option @selected($p->mode === $m)>{{ $m }}</option>@endforeach</select></div>
          <div class="col-span-2"><label class="label">Description</label><input name="description" class="input" value="{{ $p->description }}"></div>
          <div><div class="label">Block on the network</div>
            @foreach (\App\Models\Profile::CATEGORIES as $c)<label class="mr-3"><input type="checkbox" name="block_categories[]" value="{{ $c }}" @checked(in_array($c, $p->network['block_categories'] ?? []))> {{ $c }}</label>@endforeach
            <label class="block"><input type="checkbox" name="strict_egress" value="1" @checked($p->network['strict_egress'] ?? false)> strict egress (web via proxy only)</label>
            <label class="block"><input type="checkbox" name="google_udm14" value="1" @checked($p->network['google_udm14'] ?? true)> suppress Google AI Overview</label></div>
          <div><div class="label">Collect on PCs</div>
            @foreach (['window', 'clipboard', 'keys', 'files', 'gpu', 'artifacts'] as $c)<label class="mr-3"><input type="checkbox" name="collect[]" value="{{ $c }}" @checked($p->agent['collect'][$c] ?? false)> {{ $c }}</label>@endforeach
            <label class="label mt-2">Window titles</label><select name="title_capture" class="input">@foreach (['off', 'app_only', 'full'] as $t)<option @selected(($p->agent['title_capture'] ?? 'app_only') === $t)>{{ $t }}</option>@endforeach</select></div>
          <div class="col-span-2"><label class="label">Kill (opt-in) for rule ids, comma separated</label>
            <input name="kill_rules" class="input font-mono text-xs" value="{{ implode(',', array_keys($p->agent['rule_actions'] ?? [])) }}" placeholder="proc.ollama,proc.llamacpp"></div>
          </fieldset>
          @if ($isAdmin)<div class="col-span-2"><button class="btn btn-primary">Save</button></div>@endif
        </form>
        @if ($isAdmin && $p->exists && ! $p->builtin)
          <form method="post" action="{{ route('profiles.delete', $p) }}" class="mt-2">@csrf @method('delete')<button class="btn btn-danger btn-sm">Delete profile</button></form>
        @endif
      </details>
    @endforeach
  </div>
</div>
@endsection
