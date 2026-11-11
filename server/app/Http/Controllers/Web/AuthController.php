<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Services\Audit;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Auth;
use Illuminate\Support\Facades\RateLimiter;
use Illuminate\Validation\ValidationException;

class AuthController extends Controller
{
    public function show() { return view('auth.login'); }

    public function login(Request $request)
    {
        $cred = $request->validate(['email' => 'required|email', 'password' => 'required|string']);
        $key = 'login:'.strtolower($cred['email']).'|'.$request->ip();
        if (RateLimiter::tooManyAttempts($key, 5)) {
            throw ValidationException::withMessages(['email' => 'Too many attempts. Try again in '.RateLimiter::availableIn($key).' s.']);
        }
        if (! Auth::attempt($cred + ['active' => true], $request->boolean('remember'))) {
            RateLimiter::hit($key, 300);
            throw ValidationException::withMessages(['email' => 'These credentials do not match an active account.']);
        }
        RateLimiter::clear($key);
        $request->session()->regenerate();
        $request->user()->forceFill(['last_login_at' => now()])->save();
        Audit::log('auth.login');

        return redirect()->intended(route('dashboard'));
    }

    public function logout(Request $request)
    {
        if ($user = $request->user()) {
            \App\Models\ScreenViewAudit::where('instructor_id', $user->id)
                ->whereNull('ended_at')
                ->update(['ended_at' => now()]);
        }

        Audit::log('auth.logout');
        Auth::logout();
        $request->session()->invalidate();
        $request->session()->regenerateToken();

        return redirect()->route('login');
    }
}
