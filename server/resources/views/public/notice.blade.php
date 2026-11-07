@extends('layouts.app')
@section('title', __('xephyguard.title'))
@section('content')
<article class="mx-auto max-w-3xl card space-y-4 text-sm leading-relaxed">
  <div class="flex flex-wrap items-center justify-between gap-2 border-b border-slate-200 pb-3">
    <h1 class="text-2xl font-bold">{{ __('xephyguard.title') }}</h1>
    <div class="flex items-center gap-1">
      <a href="{{ url('/notice?lang=en') }}" class="btn btn-sm {{ ($lang ?? 'en') === 'en' ? 'btn-primary' : '' }}">{{ __('xephyguard.toggle_en') }}</a>
      <a href="{{ url('/notice?lang=bn') }}" class="btn btn-sm {{ ($lang ?? 'en') === 'bn' ? 'btn-primary' : '' }}">{{ __('xephyguard.toggle_bn') }}</a>
    </div>
  </div>

  <p class="text-base">{{ $text }}</p>
  <p class="text-slate-600">{{ __('xephyguard.tray_notice', ['mode' => __("xephyguard.modes.{$mode}.badge")]) }}</p>

  <div class="rounded-lg border border-slate-200 bg-slate-50 p-3 space-y-2">
    <div class="flex flex-wrap items-center gap-2">
      <span class="font-semibold">{{ __('xephyguard.active_status_heading') }}:</span>
      <span class="badge mode-{{ $mode }}">{{ __("xephyguard.modes.{$mode}.badge") }}</span>
      <span class="font-medium">{{ __("xephyguard.modes.{$mode}.name") }}</span>
    </div>
    <div class="text-xs text-slate-600">
      <b>{{ __('xephyguard.active_profile_label') }}:</b> {{ $profile->name }}
      · <b>{{ __('xephyguard.active_course_label') }}:</b>
      @if ($session)
        {{ $session->course }}
      @else
        {{ __('xephyguard.no_active_session') }}
      @endif
    </div>
    <p class="text-xs text-slate-700">{{ __("xephyguard.modes.{$mode}.summary") }}</p>
    <div class="rounded border border-slate-200 bg-white p-2 text-xs">
      @if ($mode === 'always_on' || ($aiPolicy ?? '') === 'open')
        <b>{{ __('xephyguard.ai_disclosure_mode.open_title') }} ({{ $aiPolicy ?? 'open' }}):</b>
        {{ __('xephyguard.ai_disclosure_mode.open_desc') }}
      @elseif (($aiPolicy ?? '') === 'permitted_with_disclosure' || ! $blocksAi)
        <b>{{ __('xephyguard.ai_disclosure_mode.permitted_title') }} ({{ $aiPolicy ?? 'permitted_with_disclosure' }}):</b>
        {{ __('xephyguard.ai_disclosure_mode.permitted_desc') }}
      @else
        <b>{{ __('xephyguard.ai_disclosure_mode.blocked_title') }} ({{ $aiPolicy ?? 'forbidden' }}):</b>
        {{ __('xephyguard.ai_disclosure_mode.blocked_desc', ['categories' => implode(', ', $blockCategories ?: ['ai'])]) }}
      @endif
    </div>
  </div>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.why_heading') }}</h2>
  <p>{{ __('xephyguard.why_body') }}</p>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.collection_heading') }}</h2>
  <table class="tbl">
    <tr>
      <th>{{ __('xephyguard.collection_table.signal') }}</th>
      <th>{{ __('xephyguard.collection_table.active_now') }} ({{ $profile->name }})</th>
      <th>{{ __('xephyguard.collection_table.always_on') }}</th>
      <th>{{ __('xephyguard.collection_table.practice') }}</th>
      <th>{{ __('xephyguard.collection_table.test') }}</th>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.processes_ports') }}</td>
      <td class="font-semibold">{{ ($collect['processes'] ?? false) || ($collect['extensions'] ?? false) ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.adapters_usb') }}</td>
      <td class="font-semibold">{{ ($collect['adapters'] ?? false) || ($collect['devices'] ?? false) ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.window_app') }}</td>
      <td class="font-semibold">{{ ($collect['window'] ?? false) && $titleCapture !== 'off' ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.window_title') }}</td>
      <td class="font-semibold">
        @if (!($collect['window'] ?? false) || $titleCapture === 'off')
          {{ __('xephyguard.collection_table.no') }}
        @elseif ($titleCapture === 'full')
          {{ __('xephyguard.collection_table.full') }}
        @else
          {{ __('xephyguard.collection_table.app_only') }}
        @endif
      </td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.app_only') }}</td>
      <td>{{ __('xephyguard.collection_table.app_only') }} / {{ __('xephyguard.collection_table.full') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.keystats') }}</td>
      <td class="font-semibold">{{ ($collect['keystats'] ?? false) ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.paste') }}</td>
      <td class="font-semibold">{{ ($collect['paste'] ?? false) ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.files_gpu') }}</td>
      <td class="font-semibold">{{ ($collect['files'] ?? false) || ($collect['gpu'] ?? false) ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
    <tr>
      <td>{{ __('xephyguard.collection_table.items.net_blocks') }}</td>
      <td class="font-semibold">{{ $blocksAi ? __('xephyguard.collection_table.yes') : __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.no') }}</td>
      <td>{{ __('xephyguard.collection_table.if_blocking') }}</td>
      <td>{{ __('xephyguard.collection_table.yes') }}</td>
    </tr>
  </table>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.never_heading') }}</h2>
  <p>{{ __('xephyguard.never_body') }}</p>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.isolation_heading') }}</h2>
  <p>{{ __('xephyguard.isolation_body', ['minutes' => $leaseMinutes ?? 10, 'seconds' => $leaseSeconds ?? 600]) }}</p>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.rights_heading') }}</h2>
  <p>{{ __('xephyguard.rights_body', ['retention_days' => $retentionDays ?? 140]) }}</p>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.remediation_heading') }}</h2>
  <p>{{ __('xephyguard.remediation_body') }}</p>

  <h2 class="text-lg font-semibold">{{ __('xephyguard.research_heading') }}</h2>
  <p>{{ __('xephyguard.research_body') }}</p>

  <p class="text-xs text-slate-500">{{ $lab?->name }}</p>
</article>
@endsection
