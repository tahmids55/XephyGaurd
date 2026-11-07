<?php

namespace App\Console\Commands;

use App\Models\Appeal;
use App\Models\Event;
use App\Models\KeyStat;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Score;
use App\Models\SessionPc;
use App\Models\Setting;
use App\Models\Student;
use App\Models\StudentPcAssignment;
use App\Services\Audit;
use Illuminate\Console\Command;
use Illuminate\Database\Eloquent\Builder;

class PurgeRetentionCommand extends Command
{
    protected $signature = 'xephyguard:purge
        {--days= : Override raw retention days (defaults to Setting retention_days_raw / config retention_raw_days)}
        {--dry-run : Calculate purge counts and cryptographic proof without deleting rows}';

    protected $description = 'Purge raw telemetry older than retention period while preserving legal-hold / open-appeal records and logging a cryptographic proof';

    public function handle(): int
    {
        $daysOpt = $this->option('days');
        $days = ($daysOpt !== null && $daysOpt !== '')
            ? max(1, (int) $daysOpt)
            : (int) Setting::get('retention_days_raw', config('xephyguard.retention_raw_days', config('xephyguard.retention_days_raw', 140)));
        $dryRun = (bool) $this->option('dry-run');
        $cutoff = now()->subDays($days);

        // Identify all sessions and students protected by legal_hold or an open appeal
        $openAppealSessionIds = Appeal::query()->where('status', 'open')->pluck('lab_session_id');
        $openAppealStudentIds = Appeal::query()->where('status', 'open')->pluck('student_id');

        $protectedStudents = Student::query()
            ->where('legal_hold', true)
            ->orWhereIn('id', $openAppealStudentIds)
            ->get(['id', 'login']);

        $protectedStudentIds = $protectedStudents->pluck('id')->all();
        $protectedLogins = $protectedStudents->pluck('login')
            ->filter()
            ->map(fn ($l) => strtolower(trim((string) $l)))
            ->unique()
            ->values()
            ->all();

        $studentSessionIds = collect();
        if (! empty($protectedStudentIds)) {
            $studentSessionIds = $studentSessionIds
                ->merge(Appeal::query()->whereIn('student_id', $protectedStudentIds)->pluck('lab_session_id'))
                ->merge(Score::query()->whereIn('student_id', $protectedStudentIds)->pluck('lab_session_id'))
                ->merge(SessionPc::query()->whereIn('student_id', $protectedStudentIds)->pluck('lab_session_id'))
                ->merge(StudentPcAssignment::query()->whereIn('student_id', $protectedStudentIds)->whereNotNull('lab_session_id')->pluck('lab_session_id'));
        }

        $protectedSessionIds = LabSession::query()
            ->where('legal_hold', true)
            ->pluck('id')
            ->merge($openAppealSessionIds)
            ->merge($studentSessionIds)
            ->filter()
            ->map(fn ($id) => (int) $id)
            ->unique()
            ->sort()
            ->values()
            ->all();

        $excludeProtectedSession = function (Builder $q) use ($protectedSessionIds) {
            if (! empty($protectedSessionIds)) {
                $q->where(function (Builder $w) use ($protectedSessionIds) {
                    $w->whereNull('lab_session_id')
                        ->orWhereNotIn('lab_session_id', $protectedSessionIds);
                });
            }
        };

        $excludeProtectedUser = function (Builder $q) use ($protectedLogins) {
            if (! empty($protectedLogins)) {
                $q->where(function (Builder $w) use ($protectedLogins) {
                    $w->whereNull('user')
                        ->orWhereNotIn('user', $protectedLogins);
                });
            }
        };

        $eventsQ = Event::query()->where('occurred_at', '<', $cutoff);
        $excludeProtectedSession($eventsQ);
        $excludeProtectedUser($eventsQ);

        $keyStatsQ = KeyStat::query()->where('window_start', '<', $cutoff);
        $excludeProtectedSession($keyStatsQ);
        $excludeProtectedUser($keyStatsQ);

        $pastesQ = PasteEvent::query()->where('occurred_at', '<', $cutoff);
        $excludeProtectedSession($pastesQ);
        $excludeProtectedUser($pastesQ);

        $netEventsQ = NetEvent::query()->where('occurred_at', '<', $cutoff);
        $excludeProtectedSession($netEventsQ);

        $deleted = [
            'events' => $dryRun ? $eventsQ->count() : $eventsQ->delete(),
            'key_stats' => $dryRun ? $keyStatsQ->count() : $keyStatsQ->delete(),
            'paste_events' => $dryRun ? $pastesQ->count() : $pastesQ->delete(),
            'net_events' => $dryRun ? $netEventsQ->count() : $netEventsQ->delete(),
        ];

        $proofPayload = [
            'cutoff' => $cutoff->toIso8601String(),
            'days' => $days,
            'dry_run' => $dryRun,
            'deleted' => $deleted,
            'skipped_legal_hold_session_ids' => $protectedSessionIds,
            'skipped_legal_hold_logins' => $protectedLogins,
        ];
        $proofSha256 = hash('sha256', json_encode($proofPayload, JSON_UNESCAPED_SLASHES));

        Audit::log('retention.purge', null, $proofPayload + [
            'proof_sha256' => $proofSha256,
        ]);

        $this->info(json_encode($proofPayload + ['proof_sha256' => $proofSha256], JSON_UNESCAPED_SLASHES));

        return self::SUCCESS;
    }
}
