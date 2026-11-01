@extends('layouts.app')
@section('title', 'Users')
@section('content')
<div class="card">
  <table class="tbl"><tr><th>Name</th><th>Email</th><th>Role</th><th>Active</th><th>New password</th><th></th></tr>
    @foreach ($users->push(new \App\Models\User(['role' => 'assistant', 'active' => true])) as $u)
      <tr><form method="post" action="{{ route('admin.users.save', $u->exists ? $u : null) }}">@csrf
        <td><input name="name" class="input" value="{{ $u->name }}" required></td><td><input name="email" type="email" class="input" value="{{ $u->email }}" required></td>
        <td><select name="role" class="input">@foreach (\App\Models\User::ROLES as $r)<option @selected($u->role === $r)>{{ $r }}</option>@endforeach</select></td>
        <td><input type="checkbox" name="active" value="1" @checked($u->active)></td>
        <td><input name="password" type="password" class="input" placeholder="{{ $u->exists ? 'unchanged' : 'required' }}"></td>
        <td><button class="btn btn-sm {{ $u->exists ? '' : 'btn-primary' }}">{{ $u->exists ? 'Save' : 'Add user' }}</button></td></form></tr>
    @endforeach
  </table>
  <p class="mt-2 text-xs text-slate-500">admin: setup, users, policy · instructor: sessions, reports, roster · assistant: enroll PCs, start/stop sessions, live view.</p>
</div>
@endsection
