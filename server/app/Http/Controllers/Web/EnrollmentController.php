<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Enrollment;
use App\Models\EnrollmentKey;
use App\Models\Lab;
use App\Models\Pc;
use App\Models\Setting;
use App\Services\Audit;
use App\Services\CertificateAuthority;
use App\Services\EnrollmentService;
use App\Services\NetworkPolicyWriter;
use Illuminate\Http\Request;

/** "Add a PC": enrollment key, installers, pending approvals, rename, retire. */
class EnrollmentController extends Controller
{
    public function index()
    {
        $lab = Lab::current();
        $ca = CertificateAuthority::fromConfig();
        $downloads = collect(glob(storage_path('app/public/downloads/*')) ?: [])->map(fn ($p) => basename($p));

        return view('enroll.index', [
            'lab' => $lab,
            'key' => $lab->enrollKey(),
            'enrollmentKeys' => EnrollmentKey::query()->where('lab_id', $lab->id)->with('creator:id,name')->latest()->limit(50)->get(),
            'issuedKey' => session('issued_enroll_key'),
            'ccc' => Setting::get('ccc_ip'),
            'fingerprint' => $ca->exists() ? $ca->fingerprint() : null,
            'pending' => Enrollment::query()->where('status', 'pending')->latest()->get(),
            'recent' => Enrollment::query()->where('status', '!=', 'pending')->with('pc')->latest('decided_at')->limit(10)->get(),
            'pcs' => Pc::query()->where('lab_id', $lab->id)->with('agent')->orderByRaw('retired_at is not null')->orderBy('name')->get(),
            'downloads' => $downloads,
        ]);
    }

    public function approve(Request $request, Enrollment $enrollment, EnrollmentService $svc)
    {
        abort_unless($enrollment->status === 'pending', 422, 'Already decided.');
        $d = $request->validate(['name' => 'nullable|string|max:64']);
        $pc = $svc->approve($enrollment, $request->user(), $d['name'] ?? null);

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

        return back()->with('status', 'Request rejected.');
    }

    public function rotateKey()
    {
        $lab = Lab::current();
        $lab->rotateEnrollKey();
        Audit::log('enroll.rotate_key', $lab);

        return back()->with('status', 'New enrollment key generated. The old key no longer works.');
    }

    public function createKey(Request $request)
    {
        $d = $request->validate([
            'label' => 'required|string|max:120',
            'ttl_hours' => 'nullable|integer|min:1|max:8760',
            'max_uses' => 'nullable|integer|min:1|max:10000',
            'allowed_macs' => 'nullable|string|max:20000',
        ]);
        $macs = collect(preg_split('/[\s,;]+/', strtolower($d['allowed_macs'] ?? '')))
            ->filter(fn ($m) => (bool) preg_match('/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/', $m))
            ->unique()
            ->values()
            ->all();
        $lab = Lab::current();
        $issued = EnrollmentKey::issue(
            $lab,
            $d['label'],
            isset($d['ttl_hours']) ? (int) $d['ttl_hours'] : 24,
            isset($d['max_uses']) ? (int) $d['max_uses'] : null,
            $macs ?: null,
            $request->user()
        );
        Audit::log('enroll.create_key', $issued['model'], [
            'label' => $issued['model']->label,
            'key_prefix' => $issued['model']->key_prefix,
            'expires_at' => $issued['model']->expires_at?->toIso8601String(),
            'max_uses' => $issued['model']->max_uses,
        ]);

        if ($request->expectsJson()) {
            return response()->json([
                'id' => $issued['model']->id,
                'label' => $issued['model']->label,
                'key_prefix' => $issued['model']->key_prefix,
                'key' => $issued['plain'],
                'expires_at' => $issued['model']->expires_at?->toIso8601String(),
                'max_uses' => $issued['model']->max_uses,
            ], 201);
        }

        return back()
            ->with('issued_enroll_key', $issued['plain'])
            ->with('status', "Issued short-lived key ({$issued['model']->label}). Copy it now; it will not be shown again.");
    }

    public function revokeKey(Request $request, EnrollmentKey $enrollmentKey)
    {
        $enrollmentKey->revoke();
        Audit::log('enroll.revoke_key', $enrollmentKey, ['key_prefix' => $enrollmentKey->key_prefix]);

        if ($request->expectsJson()) {
            return response()->json(['ok' => true, 'revoked_at' => $enrollmentKey->revoked_at?->toIso8601String()]);
        }

        return back()->with('status', "Revoked enrollment key {$enrollmentKey->key_prefix} ({$enrollmentKey->label}).");
    }

    public function autoApprove(Request $request)
    {
        $d = $request->validate(['auto_approve' => 'nullable|boolean', 'macs' => 'nullable|string|max:20000']);
        $macs = collect(preg_split('/[\s,;]+/', strtolower($d['macs'] ?? '')))
            ->filter(fn ($m) => preg_match('/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/', $m))->unique()->values()->all();
        $lab = Lab::current();
        $lab->update(['auto_approve' => (bool) ($d['auto_approve'] ?? false), 'auto_approve_macs' => $macs]);
        Audit::log('enroll.auto_approve', $lab, ['enabled' => $lab->auto_approve, 'macs' => count($macs)]);

        return back()->with('status', 'Auto-approve settings saved ('.count($macs).' MACs).');
    }

    public function rename(Request $request, Pc $pc)
    {
        $d = $request->validate(['name' => 'required|string|max:64']);
        Audit::log('pc.rename', $pc, ['from' => $pc->name, 'to' => $d['name']]);
        $pc->update($d);

        return back();
    }

    public function retire(Pc $pc, NetworkPolicyWriter $net)
    {
        $pc->update(['retired_at' => now(), 'seat_id' => null]);
        $pc->agents()->whereNull('revoked_at')->update(['revoked_at' => now()]);
        Audit::log('pc.retire', $pc, ['name' => $pc->name]);
        $net->write();

        return back()->with('status', "{$pc->name} retired; its agent credentials were revoked.");
    }
}
