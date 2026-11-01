@extends('layouts.app')
@section('title', 'Log in')
@section('content')
<div class="mx-auto mt-16 max-w-sm card">
  <h1 class="mb-4 text-xl font-bold">Xephy<span class="text-indigo-600">Guard</span> CCC</h1>
  <form method="post" action="{{ route('login') }}" class="space-y-3">@csrf
    <div><label class="label">Email</label><input name="email" type="email" class="input" value="{{ old('email') }}" required autofocus></div>
    <div><label class="label">Password</label><input name="password" type="password" class="input" required></div>
    <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="remember"> Keep me signed in</label>
    <button class="btn btn-primary w-full justify-center">Log in</button>
  </form>
  <p class="mt-4 text-xs text-slate-500"><a class="underline" href="{{ route('notice') }}">What does XephyGuard monitor?</a></p>
</div>
@endsection
