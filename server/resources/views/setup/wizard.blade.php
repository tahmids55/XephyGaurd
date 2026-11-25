<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<base href="/">
<title>First-run setup · XephyGuard</title>
<link rel="icon" href="favicon.ico">
<script src="js/theme.js"></script>
<link rel="stylesheet" href="css/xg.css">
<script defer src="js/app.js?v={{ filemtime(public_path('js/app.js')) }}"></script>
<script defer src="js/alpine.min.js"></script>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true"><symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></symbol><symbol id="i-moon" viewBox="0 0 24 24"><path d="M20 14.5A8 8 0 019.5 4 8 8 0 1020 14.5z"/></symbol><symbol id="i-warn" viewBox="0 0 24 24"><path d="M12 4l9.5 16.5h-19L12 4z"/><path d="M12 10v4M12 17.2v.1"/></symbol></svg>
<div x-data class="theme-float"><button class="iconbtn" @click="$store.theme.toggle()" aria-label="Toggle theme"><svg class="i" x-show="$store.theme.dark"><use href="#i-sun"/></svg><svg class="i" x-show="!$store.theme.dark" x-cloak><use href="#i-moon"/></svg></button></div>
<div class="backdrop" style="align-items:start"><div class="window xl">
  <div class="window-body stack" style="padding:36px 40px;--gap:18px">
    <div class="row" style="--gap:12px;flex-wrap:nowrap"><img src="images/logo-icon-only.svg" alt="" width="40" height="40" style="border-radius:10px"><div><h1 class="h1" style="margin:0">Welcome to XephyGuard</h1><div class="muted">Five quick steps. Everything can be changed later in Settings.</div></div></div>
    @if ($errors->any())
    <div class="note warn"><svg class="i i-sm" aria-hidden="true"><use href="#i-warn"/></svg><span>{{ $errors->first() }}</span></div>
    @endif
    <form method="post" action="{{ route('setup.store') }}" class="stack" style="--gap:18px">@csrf
      @if ($needsToken)
      <section class="fgroup">
        <div class="fhead"><b>Setup token</b></div>
        <div class="fbody"><label class="field"><span>Printed by install.sh on the CCC (not needed when you open https://127.0.0.1/setup on the CCC itself)</span><input name="setup_token" type="password" class="input mono" autocomplete="off" required></label></div>
      </section>
      @endif
      <section class="fgroup">
        <div class="fhead"><b>1. Administrator account</b></div>
        <div class="fbody cols-2">
          <label class="field"><span>Name</span><input name="name" class="input" value="{{ old('name') }}" required></label>
          <label class="field"><span>Email</span><input name="email" type="email" class="input" value="{{ old('email') }}" required></label>
          <label class="field"><span>Password (10+ characters)</span><input name="password" type="password" class="input" required></label>
          <label class="field"><span>Repeat password</span><input name="password_confirmation" type="password" class="input" required></label>
        </div>
      </section>
      <section class="fgroup">
        <div class="fhead"><b>2. Lab name</b></div>
        <div class="fbody"><input name="lab_name" class="input" value="{{ old('lab_name', $lab->name ?? 'Computer Lab 1') }}" required></div>
      </section>
      <section class="fgroup">
        <div class="fhead"><b>3. Network mode</b></div>
        <div class="fbody cols-2">
          <label class="opt"><input type="radio" name="network_mode" value="gateway" @checked(old('network_mode', $defaults['network_mode']) === 'gateway')><div><b>Gateway (recommended)</b><div class="small muted">This computer has two network cards and is the lab's default gateway. Students cannot bypass it by changing proxy settings.</div></div></label>
          <label class="opt"><input type="radio" name="network_mode" value="proxy_only" @checked(old('network_mode', $defaults['network_mode']) === 'proxy_only')><div><b>Proxy only</b><div class="small muted">The lab router forwards web traffic to this computer. Weaker; use only if gateway mode is impossible.</div></div></label>
        </div>
      </section>
      <section class="fgroup">
        <div class="fhead"><b>4. Addresses</b></div>
        <div class="fbody cols-2">
          <label class="field"><span>Lab subnet</span><input name="lab_subnet" class="input mono" value="{{ old('lab_subnet', $defaults['lab_subnet']) }}" required></label>
          <label class="field"><span>This computer's lab IP (fixed)</span><input name="ccc_ip" class="input mono" value="{{ old('ccc_ip', $defaults['ccc_ip']) }}" required></label>
        </div>
      </section>
      <section class="fgroup">
        <div class="fhead"><b>5. Privacy</b></div>
        <div class="fbody stack">
          <label class="field" style="max-width:220px"><span>Keep raw events for (days)</span><input name="retention_days_raw" type="number" class="input" value="{{ old('retention_days_raw', $defaults['retention_days_raw']) }}"></label>
          <label class="field"><span>Notice shown to students</span><textarea name="notice_text" rows="4" class="textarea">{{ old('notice_text', $defaults['notice_text']) }}</textarea><span class="help">Added automatically: “{{ $defaults['notice_generated'] }}”</span></label>
        </div>
      </section>
      <button class="btn btn-solid">Finish setup</button>
    </form>
  </div>
</div></div>
</body>
</html>
