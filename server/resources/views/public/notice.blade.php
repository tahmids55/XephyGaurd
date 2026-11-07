@extends('layouts.app')
@section('title', 'What is monitored')
@section('content')
<article class="mx-auto max-w-3xl card space-y-3 text-sm leading-relaxed">
  <h1 class="text-2xl font-bold">Lab computers are monitored by XephyGuard</h1>
  <p class="text-base">{{ $text }}</p>
  <h2 class="text-lg font-semibold">Why</h2>
  <p>To keep lab tests fair, AI tools are blocked during tests and their use is made visible. Outside tests you can browse and learn normally.</p>
  <h2 class="text-lg font-semibold">What is collected, and when</h2>
  <table class="tbl"><tr><th></th><th>No session</th><th>Practice class</th><th>Test</th></tr>
    <tr><td>Running AI programs, AI editor extensions, local AI models</td><td>yes</td><td>yes</td><td>yes</td></tr>
    <tr><td>Network adapters (hotspot/VPN), USB devices</td><td>yes</td><td>yes</td><td>yes</td></tr>
    <tr><td>Name of the program in front</td><td>no</td><td>yes</td><td>yes</td></tr>
    <tr><td>Window titles</td><td>no</td><td>no (configurable)</td><td>configurable</td></tr>
    <tr><td>Typing rhythm (number of keys, pauses — never which keys)</td><td>no</td><td>yes</td><td>yes</td></tr>
    <tr><td>Copy/paste size and whether it looks like code — never the text</td><td>no</td><td>yes</td><td>yes</td></tr>
    <tr><td>Blocked website attempts</td><td>—</td><td>if blocking is on</td><td>yes</td></tr>
  </table>
  <h2 class="text-lg font-semibold">Never collected</h2>
  <p>What you type, your clipboard text, screenshots or screen content, passwords, personal messages.</p>
  <h2 class="text-lg font-semibold">Who decides</h2>
  <p>XephyGuard only raises flags. A person reviews every flag; nothing is decided automatically. Raw data is deleted after the retention period set by your department.</p>
  <h2 class="text-lg font-semibold">Research</h2>
  <p>Data is used for research only if you gave consent, and then only under a pseudonym.</p>
  <p class="text-xs text-slate-500">{{ $lab?->name }}</p>
</article>
@endsection
