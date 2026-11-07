@extends('layouts.app')
@section('title', 'DPIA & IRB Ethics-Review Pack')
@section('content')
<div class="mx-auto max-w-5xl space-y-4">
  <div class="card flex flex-wrap items-center justify-between gap-4">
    <div>
      <h1 class="text-xl font-bold">Data Protection Impact Assessment (DPIA) & IRB Ethics-Review Pack</h1>
      <p class="text-sm text-slate-600">Generated from live configuration for <b>{{ $lab_name }}</b> · Raw retention: <b>{{ $retention_raw_days }} days</b> · Isolation lease: <b>{{ $isolation_lease_seconds }}s</b></p>
    </div>
    <div class="flex gap-2">
      <a href="{{ route('admin.ethics_pack', ['format' => 'md']) }}" class="btn btn-primary">Download Markdown (.md)</a>
    </div>
  </div>

  <div class="card">
    <pre class="whitespace-pre-wrap font-mono text-xs leading-relaxed">{{ $markdown }}</pre>
  </div>
</div>
@endsection
