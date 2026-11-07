<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\AuditLog;
use App\Models\Pc;
use App\Models\Setting;
use App\Models\TwoPersonRequest;
use App\Models\User;
use App\Services\Audit;
use App\Services\Doctor;
use App\Services\EthicsPackGenerator;
use App\Services\NetworkPolicyWriter;
use App\Services\TwoPersonGate;
use Illuminate\Http\Request;
use Illuminate\Validation\Rules\Password;

/** System health, settings, users and the audit log. */
class AdminController extends Controller
{
    public function health(Doctor $doctor)
    {
        $pcs = Pc::query()->active()->with('agent')->get();

        return view('admin.health', [
            'checks' => $doctor->run(),
            'versions' => $pcs->groupBy(fn ($p) => $p->agent?->version ?? 'unknown')->map->count(),
            'attention' => $pcs->filter(fn ($p) => ! $p->online() || $p->isolated || ($p->usage['helpers'] ?? 1) === 0
                || ($p->capabilities['keys'] ?? true) === false),
            'disk' => ['free' => @disk_free_space(storage_path()), 'total' => @disk_total_space(storage_path())],
        ]);
    }

    public function settings()
    {
        $keys = ['lab_name', 'network_mode', 'ccc_ip', 'lab_subnet', 'retention_days_raw', 'retention_days_reports',
            'notice_text', 'paste_alert_chars'];
        $values = collect($keys)->mapWithKeys(fn ($k) => [$k => Setting::get($k)])->all();
        $values['paste_alert_chars'] ??= 800;
        foreach (['always_on', 'practice', 'test'] as $m) {
            $values["title_capture_$m"] = Setting::get("privacy.title_capture.$m", $m === 'always_on' ? 'off' : 'app_only');
        }
        $values['log_urls'] = Setting::get('privacy.log_urls', ['enabled' => false, 'query' => false]);
        $values['inspect_hosts'] = implode("\n", Setting::get('inspect_hosts', NetworkPolicyWriter::DEFAULT_INSPECT_HOSTS));
        $values['google_hide_selectors'] = implode("\n", Setting::get('google_hide_selectors', []));

        return view('admin.settings', ['v' => $values]);
    }

    public function saveSettings(Request $request, NetworkPolicyWriter $net)
    {
        $d = $request->validate([
            'lab_name' => 'required|string|max:100', 'network_mode' => 'required|in:gateway,proxy_only',
            'ccc_ip' => 'required|ipv4', 'lab_subnet' => ['required', 'regex:#^\d{1,3}(\.\d{1,3}){3}/\d{1,2}$#'],
            'retention_days_raw' => 'required|integer|min:7|max:1095', 'retention_days_reports' => 'required|integer|min:30|max:3650',
            'notice_text' => 'required|string|max:2000', 'paste_alert_chars' => 'required|integer|min:50|max:100000',
            'title_capture_always_on' => 'required|in:off,app_only,full', 'title_capture_practice' => 'required|in:off,app_only,full',
            'title_capture_test' => 'required|in:off,app_only,full', 'log_urls' => 'nullable|boolean', 'log_queries' => 'nullable|boolean',
            'inspect_hosts' => 'nullable|string|max:5000', 'google_hide_selectors' => 'nullable|string|max:5000',
        ]);
        foreach (['lab_name', 'network_mode', 'ccc_ip', 'lab_subnet', 'retention_days_raw', 'retention_days_reports', 'notice_text', 'paste_alert_chars'] as $k) {
            Setting::put($k, $d[$k]);
        }
        foreach (['always_on', 'practice', 'test'] as $m) {
            Setting::put("privacy.title_capture.$m", $d["title_capture_$m"]);
        }
        Setting::put('privacy.log_urls', ['enabled' => (bool) ($d['log_urls'] ?? false), 'query' => (bool) ($d['log_queries'] ?? false)]);
        $lines = fn ($s) => array_values(array_filter(array_map('trim', preg_split('/\R/', (string) $s))));
        Setting::put('inspect_hosts', $lines($d['inspect_hosts'] ?? '') ?: NetworkPolicyWriter::DEFAULT_INSPECT_HOSTS);
        Setting::put('google_hide_selectors', $lines($d['google_hide_selectors'] ?? ''));
        \App\Models\Lab::current()->update(['name' => $d['lab_name'], 'ccc_ip' => $d['ccc_ip'], 'subnet' => $d['lab_subnet']]);
        Audit::log('settings.save', null, collect($d)->except('notice_text')->all());
        $net->write();

        return back()->with('status', 'Settings saved.');
    }

    public function users()
    {
        return view('admin.users', ['users' => User::query()->orderBy('name')->get()]);
    }

    public function saveUser(Request $request, ?User $user = null)
    {
        $d = $request->validate([
            'name' => 'required|string|max:100',
            'email' => 'required|email|max:190|unique:users,email'.($user?->exists ? ','.$user->id : ''),
            'role' => 'required|in:'.implode(',', User::ROLES),
            'password' => [$user?->exists ? 'nullable' : 'required', Password::min(10)],
            'active' => 'nullable|boolean',
        ]);
        if ($user?->exists && $user->id === $request->user()->id && ($d['role'] !== 'admin' || ! ($d['active'] ?? true))) {
            abort(422, 'You cannot demote or deactivate yourself.');
        }
        $data = ['name' => $d['name'], 'email' => $d['email'], 'role' => $d['role'], 'active' => (bool) ($d['active'] ?? true)];
        if (! empty($d['password'])) {
            $data['password'] = $d['password'];
        }
        $user = $user?->exists ? tap($user)->update($data) : User::create($data);
        Audit::log('user.save', $user, ['email' => $user->email, 'role' => $user->role, 'active' => $user->active]);

        return back()->with('status', 'User saved.');
    }

    public function audit(Request $request)
    {
        return view('admin.audit', ['entries' => AuditLog::query()->with('user:id,name')
            ->when($request->string('action')->toString(), fn ($q, $a) => $q->where('action', 'like', "$a%"))
            ->latest('id')->paginate(100)->withQueryString()]);
    }

    public function ethicsPack(Request $request, EthicsPackGenerator $generator)
    {
        $data = $generator->buildData();
        $markdown = $generator->toMarkdown($data);
        Audit::log('ethics_pack.generate', null, ['lab_name' => $data['lab_name'], 'format' => $request->query('format', 'html')]);

        if ($request->query('format') === 'md') {
            return response($markdown, 200, [
                'Content-Type' => 'text/markdown; charset=utf-8',
                'Content-Disposition' => 'attachment; filename="xephyguard-dpia-ethics-pack.md"',
            ]);
        }

        if ($request->expectsJson() || $request->query('format') === 'json') {
            return response()->json($data + ['markdown' => $markdown]);
        }

        return view('admin.ethics-pack', $data + ['markdown' => $markdown]);
    }

    public function approveTwoPerson(Request $request, TwoPersonRequest $twoPersonRequest)
    {
        $decided = TwoPersonGate::approve($twoPersonRequest, $request->user());

        if ($request->expectsJson()) {
            return response()->json(['request' => $decided]);
        }

        return back()->with('status', "Two-person request #{$decided->id} approved and executed.");
    }

    public function rejectTwoPerson(Request $request, TwoPersonRequest $twoPersonRequest)
    {
        $reason = $request->input('reason');
        $decided = TwoPersonGate::reject($twoPersonRequest, $request->user(), is_string($reason) ? $reason : null);

        if ($request->expectsJson()) {
            return response()->json(['request' => $decided]);
        }

        return back()->with('status', "Two-person request #{$decided->id} rejected.");
    }
}
