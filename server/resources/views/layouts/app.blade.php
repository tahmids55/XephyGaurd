<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta name="csrf-token" content="{{ csrf_token() }}">
    <meta name="reverb-key" content="{{ config('reverb.apps.apps.0.key') }}">
    <title>@yield('title', 'Dashboard') · XephyGuard</title>
    <link rel="icon" type="image/x-icon" href="/favicon.ico">
    <link rel="icon" type="image/svg+xml" href="/images/logo-icon-only.svg">
    @vite(['resources/css/app.css', 'resources/js/app.js'])
    <script>
    window.fmtTime = function(val) {
        if (!val) return '';
        if (typeof val === 'string' && val.length === 8 && val[2] === ':' && val[5] === ':') return val;
        try {
            const d = new Date(val);
            if (!isNaN(d.getTime())) return d.toLocaleTimeString('en-GB', { hour12: false, timeZone: 'Asia/Dhaka' });
        } catch (e) {}
        return (val || '').length > 19 ? val.substring(11, 19) : val;
    };
    </script>
</head>
<body class="min-h-screen bg-slate-100 text-slate-800">
@auth
    @php($u = auth()->user())
    <header class="bg-slate-900 text-slate-100">
        <div class="mx-auto flex max-w-screen-2xl items-center gap-6 px-4 py-2">
            <a href="{{ route('dashboard') }}" class="flex items-center gap-2 font-bold tracking-tight">
                <img src="/images/logo-icon-only.svg" alt="XephyGuard Logo" class="h-6 w-6">
                <span>Xephy<span class="text-indigo-400">Guard</span></span>
            </a>
            <nav class="flex flex-wrap gap-1 text-sm">
                @php($links = [
                    ['dashboard', 'Live lab', ['assistant', 'instructor']],
                    ['alerts.index', 'Alerts', ['assistant', 'instructor']],
                    ['sessions.index', 'Sessions', ['assistant', 'instructor']],
                    ['enroll.index', 'Add a PC', ['assistant', 'instructor']],
                    ['profiles.index', 'Timetable & profiles', ['instructor']],
                    ['reports.index', 'Reports', ['instructor']],
                    ['roster.index', 'Roster', ['instructor']],
                    ['signatures.index', 'Blocklists & signatures', ['admin']],
                    ['admin.health', 'Health', ['admin']],
                    ['admin.settings', 'Settings', ['admin']],
                    ['admin.users', 'Users', ['admin']],
                    ['admin.audit', 'Audit log', ['admin']],
                ])
                @foreach ($links as [$route, $label, $roles])
                    @if ($u->hasRole(...$roles))
                        <a href="{{ route($route) }}" @class(['rounded px-2 py-1 hover:bg-slate-700', 'bg-slate-700' => request()->routeIs($route)])>{{ $label }}</a>
                    @endif
                @endforeach
            </nav>
            <div class="ml-auto flex items-center gap-3 text-sm">
                <span class="text-slate-400">{{ $u->name }} · {{ $u->role }}</span>
                <form method="post" action="{{ route('logout') }}">@csrf<button class="rounded px-2 py-1 hover:bg-slate-700">Log out</button></form>
            </div>
        </div>
    </header>
@endauth
<main class="mx-auto max-w-screen-2xl p-4">
    @if (session('status'))
        <div class="mb-4 rounded-lg border border-emerald-300 bg-emerald-50 px-4 py-2 text-sm text-emerald-800">{{ session('status') }}</div>
    @endif
    @if ($errors->any())
        <div class="mb-4 rounded-lg border border-red-300 bg-red-50 px-4 py-2 text-sm text-red-800">
            @foreach ($errors->all() as $e)<div>{{ $e }}</div>@endforeach
        </div>
    @endif
    @yield('content')
</main>
<div x-data class="fixed right-4 bottom-4 z-50 flex w-96 flex-col gap-2">
    <template x-for="t in $store.toasts.items" :key="t.id">
        <div class="rounded-lg border bg-white px-4 py-3 text-sm shadow-lg"
             :class="{'border-red-400': ['high','critical'].includes(t.severity), 'border-amber-400': t.severity==='medium', 'border-slate-300': !['high','critical','medium'].includes(t.severity)}">
            <span class="badge mr-1" :class="'sev-' + (t.severity || 'info')" x-text="t.severity"></span><span x-text="t.title"></span>
        </div>
    </template>
</div>
</body>
</html>
