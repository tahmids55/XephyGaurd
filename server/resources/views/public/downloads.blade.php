@extends('layouts.app', ['activePage' => 'downloads.html'])
@section('title', 'Downloads')
@section('content')
@php
  $cccHost = $ccc ?: request()->getHost();
  $portSuffix = request()->getPort() && !in_array(request()->getPort(), [80, 443], true) ? ':' . request()->getPort() : '';
  $baseUrl = (request()->isSecure() ? 'https://' : 'http://') . $cccHost . $portSuffix;
  $httpBaseUrl = 'http://' . $cccHost . $portSuffix;
  $caFp = $fingerprint ?? '';
  $exeName = $setupFile ?? '';
  $exeMeta = $exeName ? collect($files ?? [])->firstWhere('name', $exeName) : null;
  $exeSize = $exeMeta ? (($exeMeta['size_label'] ?? ($exeMeta['size_mb'] . ' MB')) . ' · ') : '';
  $extraFiles = collect($files ?? [])->filter(fn ($f) => ($f['name'] ?? '') !== $exeName)->values()->all();
  $winCmd = ($exeName ?: 'XephyGuard-Setup.exe')
      . ' /S /KEY=<ENROLL_KEY>'
      . ' /CCC=' . $cccHost
      . ($caFp !== '' ? ' /CAFP=' . $caFp : '');
  $linCmd = 'curl -fsSL ' . $httpBaseUrl . '/install/linux.sh | sudo bash -s -- --key <ENROLL_KEY>';
@endphp
<script type="application/json" id="xg-page-data">{!! json_encode([
  'cccHost' => $cccHost,
  'baseUrl' => $baseUrl,
  'fp' => $caFp,
  'win' => $winCmd,
  'lin' => $linCmd,
], JSON_HEX_TAG | JSON_HEX_AMP | JSON_HEX_APOS | JSON_HEX_QUOT) !!}</script>
<div class="doc" x-data="dlPage"><div class="optbar"><span class="otitle">Downloads</span><span class="osub">Everything needed to pair a lab PC with this server.</span><span class="grow"></span></div><div class="canvas ">
<section class="panel">
  @if($exeName)
    <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#i-live"/></svg></span><div class="grow"><b>Windows installer</b><div class="muted small">{{ $exeName }} · {{ $exeSize }}Windows 10 and 11</div></div><a class="btn btn-solid" href="/downloads/{{ $exeName }}" @click="notify('Installer is downloading.')"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Download</a></div>
  @else
    <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#i-live"/></svg></span><div class="grow"><b>Windows installer</b><div class="muted small">No Windows installer package uploaded to storage yet</div></div><span class="chip plain">Not uploaded</span></div>
  @endif
  <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#i-terminal"/></svg></span><div class="grow"><b>Ubuntu and Debian installer</b><div class="muted small">linux.sh · installs the agent as a service · Xorg sessions for screen view</div></div><a class="btn" href="{{ route('install.linux') }}" @click="notify('Script is downloading.')"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Download</a></div>
  <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#i-key"/></svg></span><div class="grow"><b>Lab certificate</b><div class="muted small">ca.crt · lets the lab hide AI summaries in search results{{ $caFp !== '' ? ' · SHA-256 ' . substr($caFp, 0, 23) . '…' : '' }}</div></div><a class="btn" href="{{ route('ca.cert') }}" @click="notify('Certificate is downloading.')"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Download</a></div>
  <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#i-globe"/></svg></span><div class="grow"><b>Proxy configuration</b><div class="muted small">proxy.pac · for labs that use proxy-only mode</div></div><a class="btn" href="{{ route('proxy.pac') }}" @click="notify('File is downloading.')"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Download</a></div>
</section>
<section class="panel">
  <div class="panel-head"><h2 class="h2">Connection credentials</h2><span class="small muted">Enter these when the installer asks for server connection details.</span></div>
  <div class="panel-body stack">
    <div class="form-grid">
      <div>
        <div class="cmd-label">Teacher / CCC Server IP or Hostname</div>
        <div class="key">
          <code x-text="cccHost"></code>
          <button type="button" class="btn btn-sm" @click="xgCopy(cccHost, 'Server address copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg> Copy</button>
        </div>
      </div>
      <div>
        <div class="cmd-label">Enrollment key or one-time PC token</div>
        <p class="help">Not shown on this public page. Staff get them on <a href="{{ route('enroll.index') }}">Add a PC</a> after logging in.</p>
      </div>
    </div>
    <div>
      <div class="cmd-label">Server CA SHA-256 fingerprint</div>
      <p class="help">Before trusting a downloaded <code>ca.crt</code>, check it on the PC: <code>openssl x509 -in ca.crt -noout -fingerprint -sha256</code> (Windows: <code>certutil -hashfile ca.crt SHA256</code> on the DER file). It must match this value, which staff should also compare with the one on the CCC console.</p>
      <div class="key">
        <code x-text="fp || 'Not available yet'"></code>
        <button type="button" class="btn btn-sm" x-show="fp" @click="xgCopy(fp, 'CA fingerprint copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg> Copy</button>
      </div>
    </div>
  </div>
</section>
<div class="cols-2">
  <section class="panel panel-pad stack">
    <h2 class="h2">Silent install</h2>
    <div><div class="cmd-label">Windows (as administrator)</div><div class="cmd"><pre x-text="win"></pre><button class="btn btn-sm copy" @click="xgCopy(win, 'Copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg></button></div></div>
    <div><div class="cmd-label">Ubuntu / Debian</div><div class="cmd"><pre x-text="lin"></pre><button class="btn btn-sm copy" @click="xgCopy(lin, 'Copied')"><svg class="i i-sm" aria-hidden="true"><use href="#i-copy"/></svg></button></div></div>
  </section>
  <section class="panel panel-pad stack"><h2 class="h2">After installing</h2>
    <ol class="steps-num"><li><b>The agent starts by itself</b><div class="muted small">It runs in the background.</div></li><li><b>Approve it here</b><div class="muted small">It appears on <a href="{{ route('enroll.index') }}">Add a PC</a> within a few seconds.</div></li><li><b>If someone ends it</b><div class="muted small">The PC shows as offline on the map and an alert is raised.</div></li></ol></section>
</div>
@if(count($extraFiles))
<section class="panel">
  <div class="panel-head"><h2 class="h2">Agent binaries and helpers</h2><span class="small muted">{{ count($extraFiles) }} {{ count($extraFiles) === 1 ? 'file' : 'files' }}</span></div>
  @foreach($extraFiles as $f)
    @php
      $ext = strtolower(pathinfo($f['name'], PATHINFO_EXTENSION));
      $desc = match ($ext) {
          'cer' => 'Code signing certificate',
          'bat' => 'Helper script',
          default => 'Release artifact',
      };
      $icon = match ($ext) {
          'cer' => 'i-key',
          'bat' => 'i-terminal',
          default => 'i-download',
      };
    @endphp
    <div class="dl"><span class="ico"><svg class="i i-lg" aria-hidden="true"><use href="#{{ $icon }}"/></svg></span><div class="grow"><b>{{ $f['name'] }}</b><div class="muted small">{{ $f['size_label'] ?? ($f['size_mb'] . ' MB') }} · {{ $desc }}</div></div><a class="btn" href="/downloads/{{ $f['name'] }}" @click="notify('File is downloading.')"><svg class="i i-sm" aria-hidden="true"><use href="#i-download"/></svg> Download</a></div>
  @endforeach
</section>
@endif
</div></div>
@endsection
