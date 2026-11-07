<?php

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\Lab;
use App\Models\Profile;
use App\Models\TimetableEntry;
use App\Services\Audit;
use Illuminate\Http\Request;

/** Timetable (weekly recurrence) and profiles (reusable policy bundles). */
class ProfileController extends Controller
{
    public function index()
    {
        return view('profiles.index', [
            'profiles' => Profile::query()->orderBy('mode')->orderBy('name')->get(),
            'entries' => TimetableEntry::query()->with('profile')->orderBy('weekday')->orderBy('start_time')->get(),
        ]);
    }

    public function saveProfile(Request $request, ?Profile $profile = null)
    {
        $d = $request->validate([
            'name' => 'required|string|max:64|unique:profiles,name'.($profile?->exists ? ','.$profile->id : ''),
            'mode' => 'required|in:'.implode(',', Profile::MODES),
            'description' => 'nullable|string|max:500',
            'block_categories' => 'nullable|array', 'block_categories.*' => 'in:'.implode(',', Profile::CATEGORIES),
            'strict_egress' => 'nullable|boolean', 'google_udm14' => 'nullable|boolean',
            'title_capture' => 'required|in:off,app_only,full',
            'collect' => 'nullable|array', 'collect.*' => 'in:window,clipboard,keys,files,gpu,artifacts',
            'kill_rules' => 'nullable|string|max:1000',
        ]);
        $collect = [];
        foreach (['window', 'clipboard', 'keys', 'files', 'gpu', 'artifacts'] as $c) {
            $collect[$c] = in_array($c, $d['collect'] ?? [], true);
        }
        $ruleActions = [];
        foreach (array_filter(array_map('trim', explode(',', $d['kill_rules'] ?? ''))) as $rid) {
            $ruleActions[$rid] = ['log', 'warn', 'kill', 'alert'];
        }
        $data = [
            'name' => $d['name'], 'mode' => $d['mode'], 'description' => $d['description'] ?? null,
            'network' => ['block_categories' => array_values($d['block_categories'] ?? []),
                'strict_egress' => (bool) ($d['strict_egress'] ?? false), 'google_udm14' => (bool) ($d['google_udm14'] ?? true)],
            'agent' => array_filter(['title_capture' => $d['title_capture'], 'collect' => $collect,
                'rule_actions' => $ruleActions ?: null]),
        ];
        $profile = $profile?->exists ? tap($profile)->update($data) : Profile::create($data);
        Audit::log('profile.save', $profile, ['name' => $profile->name]);

        return back()->with('status', 'Profile saved.');
    }

    public function deleteProfile(Profile $profile)
    {
        abort_if($profile->builtin, 422, 'Built-in profiles cannot be deleted.');
        abort_if(TimetableEntry::query()->where('profile_id', $profile->id)->exists(), 422, 'Profile is used by the timetable.');
        Audit::log('profile.delete', $profile, ['name' => $profile->name]);
        $profile->delete();

        return back()->with('status', 'Profile deleted.');
    }

    public function saveEntry(Request $request)
    {
        $d = $request->validate([
            'course' => 'required|string|max:64', 'profile_id' => 'required|exists:profiles,id',
            'weekday' => 'required|integer|between:1,7', 'start_time' => 'required|date_format:H:i',
            'end_time' => 'required|date_format:H:i|after:start_time', 'valid_from' => 'nullable|date',
            'valid_until' => 'nullable|date|after_or_equal:valid_from',
        ]);
        $e = TimetableEntry::create($d + ['lab_id' => Lab::current()->id, 'enabled' => true]);
        Audit::log('timetable.add', $e, $d);

        return back()->with('status', 'Timetable entry added.');
    }

    public function toggleEntry(TimetableEntry $entry)
    {
        $entry->update(['enabled' => ! $entry->enabled]);
        Audit::log('timetable.toggle', $entry, ['enabled' => $entry->enabled]);

        return back();
    }

    public function deleteEntry(TimetableEntry $entry)
    {
        Audit::log('timetable.delete', $entry, $entry->only(['course', 'weekday', 'start_time']));
        $entry->delete();

        return back();
    }
}
