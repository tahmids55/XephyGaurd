<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Consent;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Profile;
use App\Models\Setting;
use App\Models\Student;
use App\Services\Audit;
use App\Services\TwoPersonGate;
use Carbon\Carbon;
use Illuminate\Http\Request;

/** Roster: CSV import (student id, name, login), LTI 1.3 NRPS sync, consents, exemptions, pseudonymization. */
class RosterController extends Controller
{
    public function index(Request $request)
    {
        return view('roster.index', [
            'students' => Student::query()->with('latestConsent')
                ->when($request->string('q')->toString(), fn ($q, $s) => $q->where(fn ($w) => $w->where('name', 'like', "%$s%")
                    ->orWhere('student_number', 'like', "%$s%")->orWhere('login', 'like', "%$s%")))
                ->orderBy('name')->paginate(50)->withQueryString(),
            'pseudonymize' => (bool) Setting::get('pseudonymize_exports', true),
        ]);
    }

    public function import(Request $request)
    {
        $request->validate(['file' => 'required|file|max:2048']);
        $rows = array_map('str_getcsv', preg_split('/\R/', trim($request->file('file')->get())));
        $header = array_map(fn ($h) => strtolower(trim($h)), $rows[0] ?? []);
        $hasHeader = in_array('login', $header, true) || in_array('student_id', $header, true);
        $idx = $hasHeader
            ? ['id' => array_search('student_id', $header) ?: array_search('id', $header) ?: 0,
                'name' => array_search('name', $header) ?: 1, 'login' => array_search('login', $header) ?: 2]
            : ['id' => 0, 'name' => 1, 'login' => 2];
        $n = 0;
        foreach (array_slice($rows, $hasHeader ? 1 : 0) as $r) {
            if (count($r) < 3 || trim($r[$idx['id']]) === '') {
                continue;
            }
            Student::query()->updateOrCreate(['student_number' => trim($r[$idx['id']])],
                ['name' => trim($r[$idx['name']]), 'login' => strtolower(trim($r[$idx['login']]))]);
            $n++;
        }
        Audit::log('roster.import', null, ['rows' => $n]);

        return back()->with('status', "$n students imported/updated.");
    }

    public function consent(Request $request, Student $student)
    {
        $d = $request->validate(['granted' => 'required|boolean', 'form_version' => 'nullable|string|max:16']);
        Consent::create(['student_id' => $student->id, 'purpose' => 'research', 'granted' => (bool) $d['granted'],
            'form_version' => $d['form_version'] ?? '1.0', 'recorded_by' => $request->user()->id, 'recorded_at' => now()]);
        Audit::log('consent.record', $student, ['granted' => (bool) $d['granted']]);

        return back();
    }

    public function exempt(Request $request, Student $student)
    {
        $d = $request->validate(['exempt' => 'required|boolean', 'reason' => 'nullable|required_if:exempt,1|string|max:200']);
        $pending = TwoPersonGate::requestOrRun(
            $request->user(),
            'student.exempt',
            $student,
            ['exempt' => (bool) $d['exempt'], 'reason' => $d['reason'] ?? null],
            (string) ($d['reason'] ?? 'Update student exemption'),
            function () use ($student, $d) {
                $student->update(['exempt' => (bool) $d['exempt'], 'exempt_reason' => $d['exempt'] ? $d['reason'] : null]);
                Audit::log('student.exempt', $student, $d);
            }
        );

        if ($pending) {
            return $request->expectsJson()
                ? response()->json(['status' => 'pending', 'two_person_request_id' => $pending->id, 'request' => $pending], 202)
                : back()->with('status', 'Exemption queued for two-person approval.');
        }

        return $request->expectsJson()
            ? response()->json(['status' => 'executed', 'student' => $student->fresh()])
            : back();
    }

    public function pseudonymize(Request $request)
    {
        Setting::put('pseudonymize_exports', $request->boolean('enabled'));
        Audit::log('settings.pseudonymize', null, ['enabled' => $request->boolean('enabled')]);

        return back();
    }

    /**
     * LTI 1.3 Names and Role Provisioning Services (NRPS) / JSON roster & session sync (P3-C1).
     * Accessible to authenticated instructors/admins OR via bearer token `config('xephyguard.lti_sync_token')`.
     */
    public function ltiSync(Request $request)
    {
        $user = $request->user();
        $expectedToken = (string) (Setting::get('lti_sync_token') ?: config('xephyguard.lti_sync_token', ''));
        $bearer = (string) $request->bearerToken();
        $authorizedByRole = $user && $user->hasRole('instructor');
        $authorizedByToken = $expectedToken !== '' && $bearer !== '' && hash_equals($expectedToken, $bearer);

        abort_unless($authorizedByRole || $authorizedByToken, 403, 'Unauthorized LTI roster sync request.');

        $d = $request->validate([
            'members' => 'required|array|min:1',
            'members.*' => 'required|array',
            'session' => 'nullable|array',
            'session.course' => 'required_with:session|string|max:64',
            'session.title' => 'nullable|string|max:128',
            'session.profile' => 'nullable|string|max:64',
            'session.profile_id' => 'nullable|integer|exists:profiles,id',
            'session.ai_policy' => 'nullable|in:'.implode(',', Profile::AI_POLICIES),
            'session.starts_at' => 'nullable|date',
            'session.ends_at' => 'nullable|date',
            'session.status' => 'nullable|in:scheduled,active',
        ]);

        $students = [];
        foreach ($d['members'] as $m) {
            $studentNumber = trim((string) ($m['student_number'] ?? $m['user_id'] ?? $m['id'] ?? ''));
            $name = trim((string) ($m['name'] ?? trim(($m['given_name'] ?? '').' '.($m['family_name'] ?? ''))));
            $emailPrefix = isset($m['email']) && is_string($m['email']) ? explode('@', $m['email'])[0] : '';
            $login = strtolower(trim((string) ($m['login'] ?? $m['username'] ?? $m['lis_person_sourcedid'] ?? ($emailPrefix ?: $studentNumber))));

            if ($studentNumber === '' || $name === '' || $login === '') {
                continue;
            }

            $student = Student::query()->updateOrCreate(
                ['student_number' => $studentNumber],
                ['name' => $name, 'login' => $login]
            );
            $student->ensurePortalToken();
            $students[] = [
                'id' => $student->id,
                'student_number' => $student->student_number,
                'name' => $student->name,
                'login' => $student->login,
                'pseudonym' => $student->pseudonym,
                'portal_token' => $student->portal_token,
            ];
        }

        $createdSession = null;
        if (! empty($d['session'])) {
            $sData = $d['session'];
            $profile = null;
            if (! empty($sData['profile_id'])) {
                $profile = Profile::query()->find((int) $sData['profile_id']);
            } elseif (! empty($sData['profile'])) {
                $profile = Profile::query()->where('name', $sData['profile'])->first();
            }
            $profile ??= Profile::query()->where('mode', 'test')->first() ?? Profile::query()->firstOrFail();

            $lab = Lab::current();
            $createdSession = LabSession::create([
                'lab_id' => $lab->id,
                'profile_id' => $profile->id,
                'course' => $sData['course'],
                'title' => $sData['title'] ?? null,
                'mode' => $profile->mode,
                'ai_policy' => $sData['ai_policy'] ?? ($profile->ai_policy ?? 'forbidden'),
                'status' => $sData['status'] ?? 'scheduled',
                'starts_at' => isset($sData['starts_at']) ? Carbon::parse($sData['starts_at']) : now()->addHour(),
                'ends_at' => isset($sData['ends_at']) ? Carbon::parse($sData['ends_at']) : now()->addHours(3),
                'started_by' => $user?->id,
            ]);
        }

        Audit::log('roster.lti_sync', $createdSession, [
            'synced_count' => count($students),
            'course' => $createdSession?->course,
        ]);

        return response()->json([
            'synced' => count($students),
            'students' => $students,
            'session' => $createdSession,
        ]);
    }
}
