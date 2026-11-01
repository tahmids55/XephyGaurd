@extends('layouts.app')
@section('title', 'Report')
@section('content')
<div class="card space-y-3">
  <div class="flex gap-2"><a class="btn" href="{{ route('reports.pdf', $session) }}">Export PDF</a><a class="btn" href="{{ route('reports.csv', $session) }}">Export CSV</a>
    <form method="post" action="{{ route('reports.rescore', $session) }}">@csrf<button class="btn">Re-score</button></form></div>
  <div class="prose max-w-none [&_h2]:mt-5 [&_h2]:font-semibold [&_table]:text-sm">@include('reports._body')</div>
</div>
@endsection
