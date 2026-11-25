<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Enrollment;
use App\Models\EnrollmentToken;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\Setting;
use App\Services\Audit;
use App\Services\CertificateAuthority;
use App\Services\EnrollmentService;
use App\Services\NetworkPolicyWriter;
use Illuminate\Http\Request;
use InvalidArgumentException;

/** "Add a PC": enrollment key, installers, pending approvals, rename, retire. */
class EnrollmentController extends Controller
{
    public function index()
    {
        $lab = Lab::current();
        $ca = CertificateAuthority::fromConfig();
        $downloads = collect(glob(storage_path('app/public/downloads/*')) ?: [])->map(fn ($p) => basename($p));

        $reqHost = request()->getHost();
        $ccc = (! in_array($reqHost, ['127.0.0.1', '::1', 'localhost', ''], true))
            ? $reqHost
            : Setting::get('ccc_ip', $reqHost);

        return view('enroll.index', [
            'lab' => $lab,
            'key' => $lab->enrollKey(),
            'ccc' => $ccc,
            'setupFile' => PublicController::resolveWindowsInstallerName(),
            'fingerprint' => $ca->exists() ? $ca->fingerprint() : null,
            'pending' => Enrollment::query()->where('status', 'pending')->with('conflictPc:id,name')->latest()->get(),
            'recent' => Enrollment::query()->where('status', '!=', 'pending')->with('pc')->latest('decided_at')->limit(10)->get(),
            'pcs' => Pc::query()->where('lab_id', $lab->id)->with('agent')->orderByRaw('retired_at is not null')->orderBy('name')->get(),
            'downloads' => $downloads,
            'tokens' => EnrollmentToken::query()->usable()->where('lab_id', $lab->id)->with('creator:id,name')->latest()->get(),
        ]);
    }

    /** One-time per-PC enrollment token; the plain value is shown once and stored hashed (S-14). */
    public function createToken(Request $request)
    {
        $d = $request->validate(['pc_name' => 'nullable|string|max:64', 'hours' => 'nullable|integer|min:1|max:720']);
        [$row, $plain] = EnrollmentToken::issue(Lab::current(), $request->user(), $d['pc_name'] ?? null, (int) ($d['hours'] ?? 72));
        Audit::log('enrollment.token_create', $row, ['pc_name' => $row->pc_name, 'expires_at' => $row->expires_at->toIso8601String()]);

        return back()->with('enroll_token', $plain)->with('status', 'One-time enrollment token created. Copy it now; it is not shown again.');
    }

    public function revokeToken(EnrollmentToken $token)
    {
        $token->forceFill(['revoked_at' => now()])->save();
        Audit::log('enrollment.token_revoke', $token, ['pc_name' => $token->pc_name]);

        return back()->with('status', 'Enrollment token revoked.');
    }

    public function approve(Request $request, Enrollment $enrollment, EnrollmentService $svc)
    {
        abort_unless($enrollment->status === 'pending', 422, 'Already decided.');
        $d = $request->validate(['name' => 'nullable|string|max:64', 'adopt_pc_id' => 'nullable|integer|exists:pcs,id']);
        $adopt = isset($d['adopt_pc_id']) ? Pc::findOrFail((int) $d['adopt_pc_id']) : null;   // explicit staff choice (S-17)
        try {
            $pc = $svc->approve($enrollment, $request->user(), $d['name'] ?? null, 'staff', $adopt);
        } catch (InvalidArgumentException $ex) {
            abort(422, $ex->getMessage());
        }
        $pc->loadMissing('agent');

        if ($request->expectsJson()) {
            return response()->json([
                'ok' => true,
                'pc' => [
                    'id' => $pc->id,
                    'real' => true,
                    'name' => $pc->name,
                    'host' => $pc->hostname,
                    'os' => trim($pc->os.' '.($pc->os_version ?? '')),
                    'ip' => $pc->ip ?? '—',
                    'agent' => $pc->agent?->version ?? '—',
                    'status' => $pc->online() ? 'normal' : 'offline',
                    'lastSeen' => 'Never',
                    'certExpires' => $pc->agent?->cert_expires_at?->format('Y-m-d') ?? '—',
                    'retired' => false,
                    'editingName' => false,
                ],
            ]);
        }

        return back()->with('status', "Approved {$pc->name}. The agent picks up its certificate within seconds.");
    }

    public function approveAll(Request $request, EnrollmentService $svc)
    {
        $n = 0;
        foreach (Enrollment::query()->where('status', 'pending')->get() as $e) {
            $svc->approve($e, $request->user());
            $n++;
        }

        return back()->with('status', "Approved $n PCs.");
    }

    public function reject(Request $request, Enrollment $enrollment, EnrollmentService $svc)
    {
        $svc->reject($enrollment, $request->user());

        return $request->expectsJson()
            ? response()->json(['ok' => true])
            : back()->with('status', 'Request rejected.');
    }

    public function rotateKey()
    {
        $lab = Lab::current();
        $unused = EnrollmentToken::query()->usable()->where('lab_id', $lab->id)->count();
        $lab->rotateEnrollKey();
        Audit::log('enroll.rotate_key', $lab, ['tokens_revoked' => $unused]);

        return back()->with('status', 'New enrollment key generated. The old key and '.$unused.' unused PC token(s) no longer work.');
    }

    public function rename(Request $request, Pc $pc)
    {
        $d = $request->validate(['name' => 'required|string|max:64']);
        Audit::log('pc.rename', $pc, ['from' => $pc->name, 'to' => $d['name']]);
        $pc->update($d);

        return $request->expectsJson() ? response()->json(['ok' => true]) : back();
    }

    public function retire(Request $request, Pc $pc, NetworkPolicyWriter $net)
    {
        $pc->update(['retired_at' => now(), 'seat_id' => null]);
        $pc->agents()->whereNull('revoked_at')->update(['revoked_at' => now()]);
        Audit::log('pc.retire', $pc, ['name' => $pc->name]);
        $net->write();

        return $request->expectsJson()
            ? response()->json(['ok' => true])
            : back()->with('status', "{$pc->name} retired; its agent credentials were revoked.");
    }
}
