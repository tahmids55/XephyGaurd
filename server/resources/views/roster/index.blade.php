@extends('layouts.app')
@section('title', 'Roster')
@section('content')
<div class="card">
  <div class="mb-3 flex flex-wrap items-end gap-3">
    <form method="post" action="{{ route('roster.import') }}" enctype="multipart/form-data" class="flex items-end gap-2">@csrf
      <div><label class="label">Import CSV (student_id, name, login)</label><input type="file" name="file" class="input" required></div><button class="btn btn-primary">Import</button></form>
    <form class="flex items-end gap-2"><input name="q" value="{{ request('q') }}" class="input w-64" placeholder="Search"><button class="btn">Search</button></form>
    @if (auth()->user()->hasRole('admin'))
      <form method="post" action="{{ route('roster.pseudonymize') }}" class="ml-auto">@csrf
        <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="enabled" value="1" @checked($pseudonymize) onchange="this.form.submit()"> Pseudonymize research exports</label></form>
    @endif
  </div>
  <table class="tbl"><tr><th>Student id</th><th>Name</th><th>OS login</th><th>Pseudonym</th><th>Research consent</th><th>Exemption (false-positive guard)</th></tr>
    @foreach ($students as $s)
      <tr><td>{{ $s->student_number }}</td><td>{{ $s->name }}</td><td class="font-mono text-xs">{{ $s->login }}</td><td class="font-mono text-xs">{{ $s->pseudonym }}</td>
        <td><form method="post" action="{{ route('roster.consent', $s) }}" class="flex items-center gap-1">@csrf
          <span @class(['badge', 'bg-emerald-100 text-emerald-800' => $s->latestConsent?->granted, 'sev-info' => ! $s->latestConsent?->granted])>{{ $s->latestConsent ? ($s->latestConsent->granted ? 'granted' : 'declined') : 'none' }}</span>
          <select name="granted" class="input w-24"><option value="1">grant</option><option value="0">decline</option></select><button class="btn btn-sm">Record</button></form></td>
        <td><form method="post" action="{{ route('roster.exempt', $s) }}" class="flex items-center gap-1">@csrf
          <input type="hidden" name="exempt" value="{{ $s->exempt ? 0 : 1 }}">
          @if ($s->exempt)<span class="text-xs">{{ $s->exempt_reason }}</span><button class="btn btn-sm">Remove</button>
          @else<input name="reason" class="input w-48" placeholder="e.g. speech-to-text tool"><button class="btn btn-sm">Exempt</button>@endif</form></td></tr>
    @endforeach
  </table>{{ $students->links() }}
</div>
@endsection
