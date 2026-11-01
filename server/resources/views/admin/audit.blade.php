@extends('layouts.app')
@section('title', 'Audit log')
@section('content')
<div class="card">
  <form class="mb-2 flex gap-2"><input name="action" class="input w-64" value="{{ request('action') }}" placeholder="action prefix, e.g. session."><button class="btn">Filter</button></form>
  <table class="tbl"><tr><th>Time</th><th>User</th><th>Action</th><th>Subject</th><th>Details</th><th>IP</th></tr>
    @foreach ($entries as $e)<tr><td class="font-mono text-xs whitespace-nowrap">{{ $e->created_at->format('Y-m-d H:i:s') }}</td><td>{{ $e->user?->name ?? 'system' }}</td>
      <td class="font-mono text-xs">{{ $e->action }}</td><td class="text-xs">{{ $e->subject_type }} {{ $e->subject_id }}</td>
      <td class="font-mono text-xs break-all">{{ $e->details ? json_encode($e->details) : '' }}</td><td class="text-xs">{{ $e->ip }}</td></tr>@endforeach
  </table>{{ $entries->links() }}
  <p class="mt-2 text-xs text-slate-500">The audit log is append-only.</p>
</div>
@endsection
