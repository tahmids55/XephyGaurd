<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Consent;
use App\Models\Setting;
use App\Models\Student;
use App\Services\Audit;
use Illuminate\Http\Request;

/** Roster: CSV import (student id, name, login), consents, exemptions, pseudonymization. */
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
        $student->update(['exempt' => (bool) $d['exempt'], 'exempt_reason' => $d['exempt'] ? $d['reason'] : null]);
        Audit::log('student.exempt', $student, $d);

        return back();
    }

    public function pseudonymize(Request $request)
    {
        Setting::put('pseudonymize_exports', $request->boolean('enabled'));
        Audit::log('settings.pseudonymize', null, ['enabled' => $request->boolean('enabled')]);

        return back();
    }
}
