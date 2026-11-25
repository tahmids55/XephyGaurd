<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Consent;
use App\Models\Setting;
use App\Models\Student;
use App\Services\Audit;
use App\Services\RosterImport;
use Illuminate\Http\Request;
use Illuminate\Validation\ValidationException;
use InvalidArgumentException;

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
        try {
            $import = RosterImport::parse((string) $request->file('file')->get());
        } catch (InvalidArgumentException $e) {
            throw ValidationException::withMessages(['file' => $e->getMessage()]);
        }
        foreach ($import->rows as $r) {
            Student::query()->updateOrCreate(['student_number' => $r['student_number']],
                ['name' => $r['name'], 'login' => $r['login']]);
        }
        $n = count($import->rows);
        Audit::log('roster.import', null, ['rows' => $n, 'skipped' => count($import->skipped)]);

        $status = "$n students imported/updated.";
        if ($import->skipped !== []) {
            $first = array_map(fn ($s) => "line {$s['line']}: {$s['reason']}", array_slice($import->skipped, 0, 5));
            $status .= ' '.count($import->skipped).' rows skipped ('.implode('; ', $first).(count($import->skipped) > 5 ? '; ...' : '').').';
        }

        return back()->with('status', $status);
    }

    public function consent(Request $request, Student $student)
    {
        $d = $request->validate(['granted' => 'required|boolean', 'form_version' => 'nullable|string|max:16']);
        Consent::create(['student_id' => $student->id, 'purpose' => 'research', 'granted' => (bool) $d['granted'],
            'form_version' => $d['form_version'] ?? '1.0', 'recorded_by' => $request->user()->id, 'recorded_at' => now()]);
        Audit::log('consent.record', $student, ['granted' => (bool) $d['granted']]);

        return $request->expectsJson() ? response()->json(['ok' => true]) : back();
    }

    public function exempt(Request $request, Student $student)
    {
        $d = $request->validate(['exempt' => 'required|boolean', 'reason' => 'nullable|required_if:exempt,1|string|max:200']);
        $student->update(['exempt' => (bool) $d['exempt'], 'exempt_reason' => $d['exempt'] ? $d['reason'] : null]);
        // The reason may be health data (ETHICS): the audit log records only that the exemption changed.
        Audit::log('student.exempt', $student, ['exempt' => (bool) $d['exempt']]);

        return $request->expectsJson() ? response()->json(['ok' => true]) : back();
    }

    public function pseudonymize(Request $request)
    {
        Setting::put('pseudonymize_exports', $request->boolean('enabled'));
        Audit::log('settings.pseudonymize', null, ['enabled' => $request->boolean('enabled')]);

        return $request->expectsJson() ? response()->json(['ok' => true]) : back();
    }
}
