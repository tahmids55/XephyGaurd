@extends('layouts.app')
@section('title', 'Settings')
@section('content')
<form method="post" action="{{ route('admin.settings.save') }}" class="grid gap-4 lg:grid-cols-2">@csrf
  <div class="card space-y-3"><h2 class="font-semibold">Lab and network</h2>
    <div><label class="label">Lab name</label><input name="lab_name" class="input" value="{{ $v['lab_name'] }}"></div>
    <div><label class="label">Network mode</label><select name="network_mode" class="input"><option value="gateway" @selected($v['network_mode'] === 'gateway')>gateway (recommended)</option><option value="proxy_only" @selected($v['network_mode'] === 'proxy_only')>proxy only</option></select></div>
    <div class="grid grid-cols-2 gap-3"><div><label class="label">CCC IP</label><input name="ccc_ip" class="input" value="{{ $v['ccc_ip'] }}"></div>
      <div><label class="label">Lab subnet</label><input name="lab_subnet" class="input" value="{{ $v['lab_subnet'] }}"></div></div>
    <div class="grid grid-cols-2 gap-3"><div><label class="label">Timezone (IANA)</label><input name="timezone" class="input font-mono text-xs" placeholder="UTC" value="{{ $v['timezone'] }}"></div>
      <div><label class="label">Search engine override URL (udm=14)</label><input name="google_search_url" class="input font-mono text-xs" placeholder="https://www.google.com/search?q={searchTerms}&udm=14" value="{{ $v['google_search_url'] }}"></div></div>
    <div><label class="label">Hosts inspected (TLS interception only for these, only for restricted PCs)</label><textarea name="inspect_hosts" rows="5" class="input font-mono text-xs">{{ $v['inspect_hosts'] }}</textarea></div>
    <div><label class="label">Fallback CSS selectors hiding search AI blocks</label><textarea name="google_hide_selectors" rows="3" class="input font-mono text-xs">{{ $v['google_hide_selectors'] }}</textarea></div>
    <div><label class="label">Alert on pastes larger than (characters, Test mode)</label><input type="number" name="paste_alert_chars" class="input w-32" value="{{ $v['paste_alert_chars'] }}"></div>
  </div>
  <div class="card space-y-3"><h2 class="font-semibold">Privacy and retention</h2>
    <div class="grid grid-cols-2 gap-3"><div><label class="label">Raw events kept (days)</label><input type="number" name="retention_days_raw" class="input" value="{{ $v['retention_days_raw'] }}"></div>
      <div><label class="label">Reports kept (days)</label><input type="number" name="retention_days_reports" class="input" value="{{ $v['retention_days_reports'] }}"></div></div>
    <div class="grid grid-cols-3 gap-3">@foreach (['always_on' => 'Always-on', 'practice' => 'Practice', 'test' => 'Test'] as $m => $label)
      <div><label class="label">Window titles: {{ $label }}</label><select name="title_capture_{{ $m }}" class="input">@foreach (['off', 'app_only', 'full'] as $t)<option @selected($v["title_capture_$m"] === $t)>{{ $t }}</option>@endforeach</select></div>@endforeach</div>
    <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="log_urls" value="1" @checked($v['log_urls']['enabled'] ?? false)> Log visited hosts/paths of permitted sites (restricted PCs only)</label>
    <label class="flex items-center gap-2 text-sm"><input type="checkbox" name="log_queries" value="1" @checked($v['log_urls']['query'] ?? false)> …including query strings (search terms)</label>
    <div><label class="label">Student notice / login banner text</label><textarea name="notice_text" rows="5" class="input">{{ $v['notice_text'] }}</textarea>
      <p class="text-xs text-slate-500">Shown on the PCs' indicator and on the public page <a class="underline" href="{{ route('notice') }}">/notice</a>.</p></div>
  </div>
  <div class="lg:col-span-2"><button class="btn btn-primary">Save settings</button></div>
</form>
@endsection
