<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Lab;
use App\Models\Setting;
use App\Models\User;
use App\Services\Audit;
use App\Services\NetworkPolicyWriter;
use App\Services\NoticeText;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Auth;
use Illuminate\Validation\Rules\Password;

/**
 * First-run wizard: admin account, lab name, network mode, subnet/CCC IP, retention, notice text.
 * Until an admin exists, submitting it needs the one-time XG_SETUP_TOKEN (printed by install.sh)
 * unless the request comes straight from loopback on the CCC itself (I-SERVER-22, I-CCC-6).
 */
class SetupController extends Controller
{
    public function show(Request $request)
    {
        abort_if(User::query()->where('role', 'admin')->exists(), 404);

        return view('setup.wizard', [
            'needsToken' => ! $this->isDirectLoopback($request),
            'lab' => Lab::query()->first(),
            'defaults' => [
                'ccc_ip' => Setting::get('ccc_ip'), 'lab_subnet' => Setting::get('lab_subnet'),
                'network_mode' => Setting::get('network_mode'), 'retention_days_raw' => Setting::get('retention_days_raw'),
                'notice_text' => NoticeText::adminText(), 'notice_generated' => NoticeText::featureSentences(),
            ],
        ]);
    }

    public function store(Request $request, NetworkPolicyWriter $net)
    {
        abort_if(User::query()->where('role', 'admin')->exists(), 404);
        $via = $this->isDirectLoopback($request) ? 'loopback' : 'token';
        if ($via === 'token' && ! $this->tokenMatches($request->input('setup_token'))) {
            return back()->withInput($request->except(['password', 'password_confirmation', 'setup_token']))
                ->withErrors(['setup_token' => 'Setup token missing or wrong. Use the token install.sh printed, or open the wizard on the CCC itself (https://127.0.0.1/setup).']);
        }
        $d = $request->validate([
            'name' => 'required|string|max:100',
            'email' => 'required|email|max:190',
            'password' => ['required', 'confirmed', Password::min(10)],
            'lab_name' => 'required|string|max:100',
            'network_mode' => 'required|in:gateway,proxy_only',
            'lab_subnet' => ['required', 'regex:#^\d{1,3}(\.\d{1,3}){3}/\d{1,2}$#'],
            'ccc_ip' => 'required|ipv4',
            'retention_days_raw' => 'required|integer|min:7|max:1095',
            'notice_text' => 'required|string|max:2000',
        ]);
        $user = User::create(['name' => $d['name'], 'email' => $d['email'], 'password' => $d['password'], 'role' => 'admin']);
        $lab = Lab::query()->firstOrCreate([], ['name' => $d['lab_name']]);
        $lab->update(['name' => $d['lab_name'], 'subnet' => $d['lab_subnet'], 'ccc_ip' => $d['ccc_ip']]);
        if (! $lab->enroll_key_hash) {
            $lab->rotateEnrollKey();
        }
        foreach (['network_mode', 'lab_subnet', 'ccc_ip', 'retention_days_raw', 'notice_text'] as $k) {
            Setting::put($k, $d[$k]);
        }
        Setting::put('lab_name', $d['lab_name']);
        Auth::login($user);
        Audit::log('setup.complete', $lab, ['network_mode' => $d['network_mode'], 'subnet' => $d['lab_subnet'], 'via' => $via]);
        $net->write();

        return redirect()->route('enroll.index')->with('status', 'Setup complete. Next: add your lab PCs.');
    }

    /** Loopback peer with no proxy in between (nginx fastcgi clears X-Forwarded-For; an HTTP proxy sets it). */
    private function isDirectLoopback(Request $request): bool
    {
        $peer = (string) $request->server('REMOTE_ADDR', '');

        return in_array($peer, ['127.0.0.1', '::1'], true) && (string) $request->headers->get('X-Forwarded-For', '') === '';
    }

    private function tokenMatches(mixed $given): bool
    {
        $expected = (string) config('xephyguard.setup_token', '');

        return strlen($expected) >= 16 && is_string($given) && hash_equals($expected, $given);
    }
}
