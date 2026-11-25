@extends('layouts.app', ['activePage' => 'settings.html'])
@section('title', 'Settings')
@section('content')
@php
  $netMode = ($v['network_mode'] ?? 'proxy_only') === 'gateway' ? 'gateway' : 'proxy_only';
  $tAlwaysRaw = $v['title_capture_always_on'] ?? 'off';
  $tPracticeRaw = $v['title_capture_practice'] ?? 'app_only';
  $tTestRaw = $v['title_capture_test'] ?? 'app_only';
  $tAlways = $tAlwaysRaw === 'app_only' ? 'app' : $tAlwaysRaw;
  $tPractice = $tPracticeRaw === 'app_only' ? 'app' : $tPracticeRaw;
  $tTest = $tTestRaw === 'app_only' ? 'app' : $tTestRaw;
  $logHosts = (bool) ($v['log_urls']['enabled'] ?? false);
  $logQueries = $logHosts && (bool) ($v['log_urls']['query'] ?? false);
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'mode' => $netMode,
  'pasteAt' => (int) ($v['paste_alert_chars'] ?? 800),
  'tAlways' => $tAlways,
  'tPractice' => $tPractice,
  'tTest' => $tTest,
  'hosts' => $logHosts,
  'queries' => $logQueries,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="settingsPage"><div class="optbar"><span class="otitle">Settings</span><span class="osub">Network, privacy and the notice students see.</span><span class="grow"></span></div><div class="canvas ">
  @if ($errors->any())
  <div class="note warn" style="margin-bottom:16px"><svg class="i i-sm" aria-hidden="true"><use href="#i-warn"/></svg><span>{{ $errors->first() }}</span></div>
  @endif
  <div class="cols-side" style="grid-template-columns:200px minmax(0,1fr)">
    <nav class="index" aria-label="Sections">
      <a href="#network" :class="cur === 'network' && 'on'" @click.prevent="go('network')">Network</a>
      <a href="#search" :class="cur === 'search' && 'on'" @click.prevent="go('search')">Search AI summaries</a>
      <a href="#privacy" :class="cur === 'privacy' && 'on'" @click.prevent="go('privacy')">Privacy and retention</a>
      <a href="#notice" :class="cur === 'notice' && 'on'" @click.prevent="go('notice')">Student notice</a>
    </nav>
    <form method="post" action="{{ route('admin.settings.save') }}">
      @csrf
      <input type="hidden" name="title_capture_always_on" value="{{ $tAlwaysRaw }}" :value="tAlways === 'app' ? 'app_only' : tAlways">
      <input type="hidden" name="title_capture_practice" value="{{ $tPracticeRaw }}" :value="tPractice === 'app' ? 'app_only' : tPractice">
      <input type="hidden" name="title_capture_test" value="{{ $tTestRaw }}" :value="tTest === 'app' ? 'app_only' : tTest">
      <section class="section" id="network"><div class="panel-head"><h2 class="h2">Network</h2></div>
        <div class="panel-body stack" style="max-width:760px;--gap:20px">
          <div class="form-grid">
            <label class="field"><span>Lab name</span><input name="lab_name" class="input" value="{{ $v['lab_name'] ?? '' }}" required></label>
            <label class="field"><span>Time zone</span>
              <select name="timezone" class="select">
                @php $tz = $v['timezone'] ?? 'UTC'; @endphp
                <option value="Asia/Dhaka" @selected($tz === 'Asia/Dhaka')>Asia/Dhaka</option>
                <option value="UTC" @selected($tz === 'UTC')>UTC</option>
                @if(!in_array($tz, ['Asia/Dhaka', 'UTC'], true))
                  <option value="{{ $tz }}" selected>{{ $tz }}</option>
                @endif
              </select>
            </label>
          </div>
          <div><div class="label">How traffic reaches this server</div><div class="stack" style="--gap:8px">
            <label class="opt"><input type="radio" name="network_mode" value="gateway" x-model="mode"><span><b>Gateway mode</b><small>This server is the lab’s default gateway. Students can’t get around blocking with browser proxy settings.</small></span></label>
            <label class="opt"><input type="radio" name="network_mode" value="proxy_only" x-model="mode"><span><b>Proxy only</b><small>The router sends web traffic here. Easier to set up, easier to get around.</small></span></label></div></div>
          <div class="form-grid"><label class="field"><span>Server address</span><input name="ccc_ip" class="input mono" value="{{ $v['ccc_ip'] ?? request()->getHost() }}" required></label><label class="field"><span>Lab subnet</span><input name="lab_subnet" class="input mono" value="{{ $v['lab_subnet'] ?? '' }}" required></label></div>
        </div></section>
      <section class="section" id="search"><div class="panel-head"><h2 class="h2">Search AI summaries</h2></div>
        <div class="panel-body stack" style="max-width:760px;--gap:18px">
          <label class="field"><span>Screen view CPU budget (% of one core)</span><input name="screen_cpu_budget_pct" type="number" min="1" max="100" class="input mono" value="{{ $v['screen_cpu_budget_pct'] ?? 8 }}"><p class="help">Live screen view on a lab PC lowers its frame rate (not below 2 fps), then quality, to stay under this. Default 8.</p></label>
          <label class="field"><span>Search engine override URL (udm=14)</span><input name="google_search_url" class="input mono" value="{{ $v['google_search_url'] ?? 'https://www.google.com/search?q={searchTerms}&udm=14' }}"><p class="help">Forces web-only search results without generative AI overviews when enabled in a profile.</p></label>
          <label class="field"><span>Search sites to inspect</span><textarea name="inspect_hosts" class="textarea mono" rows="3">{{ !empty($v['inspect_hosts']) ? $v['inspect_hosts'] : "www.google.com\nsearch.yahoo.com\nduckduckgo.com" }}</textarea><p class="help">Only these sites are opened up so AI summary boxes can be hidden.</p></label>
          <label class="field"><span>Selectors that hide AI summaries</span><textarea name="google_hide_selectors" class="textarea mono" rows="2">{{ !empty($v['google_hide_selectors']) ? $v['google_hide_selectors'] : 'div[data-attrid="AIOverview"], #m-x-content, g-ai-overview' }}</textarea></label>
          <label class="field" style="max-width:260px"><span>Alert on pastes over (characters)</span><input name="paste_alert_chars" class="input" type="number" x-model.number="pasteAt" required></label>
        </div></section>
      <section class="section" id="privacy"><div class="panel-head"><h2 class="h2">Privacy and retention</h2></div>
        <div class="panel-body stack" style="max-width:760px;--gap:20px">
          <div class="form-grid"><label class="field"><span>Keep raw activity for (days)</span><input name="retention_days_raw" class="input" type="number" value="{{ $v['retention_days_raw'] ?? 140 }}" required></label><label class="field"><span>Keep finished reports for (days)</span><input name="retention_days_reports" class="input" type="number" value="{{ $v['retention_days_reports'] ?? 1825 }}" required></label></div>
          <div><div class="label">Window titles</div>
            <table class="tbl tight" style="max-width:560px;border:1px solid var(--border);border-radius:8px"><tbody>
              <tr><td>Outside class</td><td class="right"><div class="seg"><button type="button" :aria-pressed="tAlways === 'off'" @click="tAlways = 'off'">Off</button><button type="button" :aria-pressed="tAlways === 'app'" @click="tAlways = 'app'">Program</button><button type="button" :aria-pressed="tAlways === 'full'" @click="tAlways = 'full'">Full</button></div></td></tr>
              <tr><td>Practice class</td><td class="right"><div class="seg"><button type="button" :aria-pressed="tPractice === 'off'" @click="tPractice = 'off'">Off</button><button type="button" :aria-pressed="tPractice === 'app'" @click="tPractice = 'app'">Program</button><button type="button" :aria-pressed="tPractice === 'full'" @click="tPractice = 'full'">Full</button></div></td></tr>
              <tr><td>Test or exam</td><td class="right"><div class="seg"><button type="button" :aria-pressed="tTest === 'off'" @click="tTest = 'off'">Off</button><button type="button" :aria-pressed="tTest === 'app'" @click="tTest = 'app'">Program</button><button type="button" :aria-pressed="tTest === 'full'" @click="tTest = 'full'">Full</button></div></td></tr></tbody></table></div>
          <div class="stack" style="--gap:12px"><label class="check"><span class="switch"><input type="checkbox" name="log_urls" value="1" x-model="hosts"><i></i></span> Log which allowed sites are visited</label><label class="check" x-show="hosts"><span class="switch"><input type="checkbox" name="log_queries" value="1" x-model="queries"><i></i></span> <span>Include search terms <span class="muted">(off by default)</span></span></label></div>
        </div></section>
      <section class="section" id="notice"><div class="panel-head"><h2 class="h2">Student notice</h2><a class="small" href="{{ route('notice') }}">Preview</a></div>
        <div class="panel-body" style="max-width:760px"><textarea name="notice_text" class="textarea" rows="4" required>{{ $v['notice_text'] }}</textarea><p class="help">Shown on the agent’s indicator and on the public notice page. Added automatically from the enabled features: “{{ $v['notice_generated'] }}”</p></div></section>
      <div class="savebar"><span class="muted small grow">Changes apply to every PC within a few seconds.</span><button type="button" class="btn" @click="reload()">Discard</button><button type="submit" class="btn btn-solid">Save settings</button></div>
    </form>
  </div>
</div></div>
@endsection
