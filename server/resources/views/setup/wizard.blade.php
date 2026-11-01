@extends('layouts.app')
@section('title', 'First-run setup')
@section('content')
<div class="mx-auto max-w-2xl card">
  <h1 class="text-xl font-bold">Welcome to XephyGuard</h1>
  <p class="mb-4 text-sm text-slate-600">Five quick steps. Everything can be changed later in Settings.</p>
  <form method="post" action="{{ route('setup.store') }}" class="space-y-5">@csrf
    <fieldset class="space-y-2"><legend class="font-semibold">1. Administrator account</legend>
      <div class="grid grid-cols-2 gap-3">
        <div><label class="label">Name</label><input name="name" class="input" value="{{ old('name') }}" required></div>
        <div><label class="label">Email</label><input name="email" type="email" class="input" value="{{ old('email') }}" required></div>
        <div><label class="label">Password (10+ characters)</label><input name="password" type="password" class="input" required></div>
        <div><label class="label">Repeat password</label><input name="password_confirmation" type="password" class="input" required></div>
      </div></fieldset>
    <fieldset class="space-y-2"><legend class="font-semibold">2. Lab name</legend>
      <input name="lab_name" class="input" value="{{ old('lab_name', $lab->name ?? 'Computer Lab 1') }}" required></fieldset>
    <fieldset class="space-y-2"><legend class="font-semibold">3. Network mode</legend>
      <label class="flex gap-2 text-sm"><input type="radio" name="network_mode" value="gateway" @checked(old('network_mode', $defaults['network_mode']) === 'gateway')>
        <span><b>Gateway (recommended)</b> — this computer has two network cards and is the lab's default gateway. Students cannot bypass it by changing proxy settings.</span></label>
      <label class="flex gap-2 text-sm"><input type="radio" name="network_mode" value="proxy_only" @checked(old('network_mode', $defaults['network_mode']) === 'proxy_only')>
        <span><b>Proxy only</b> — the lab router forwards web traffic to this computer. Weaker; use only if gateway mode is impossible.</span></label></fieldset>
    <fieldset class="grid grid-cols-2 gap-3"><legend class="col-span-2 font-semibold">4. Addresses</legend>
      <div><label class="label">Lab subnet</label><input name="lab_subnet" class="input" value="{{ old('lab_subnet', $defaults['lab_subnet']) }}" required></div>
      <div><label class="label">This computer's lab IP (fixed)</label><input name="ccc_ip" class="input" value="{{ old('ccc_ip', $defaults['ccc_ip']) }}" required></div>
      <p class="col-span-2 text-xs text-slate-500">Tip: give lab PCs DHCP reservations so each PC keeps its IP.</p></fieldset>
    <fieldset class="space-y-2"><legend class="font-semibold">5. Privacy</legend>
      <div><label class="label">Keep raw events for (days)</label><input name="retention_days_raw" type="number" class="input w-32" value="{{ old('retention_days_raw', $defaults['retention_days_raw']) }}"></div>
      <div><label class="label">Notice shown to students</label><textarea name="notice_text" rows="4" class="input">{{ old('notice_text', $defaults['notice_text']) }}</textarea></div></fieldset>
    <button class="btn btn-primary">Finish setup</button>
  </form>
</div>
@endsection
