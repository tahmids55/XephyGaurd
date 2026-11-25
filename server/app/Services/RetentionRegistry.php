<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\Alert;
use App\Models\Command;
use App\Models\Event;
use App\Models\EvidenceCapture;
use App\Models\KeyStat;
use App\Models\NetEvent;
use App\Models\PasteEvent;
use App\Models\Report;
use App\Models\Score;
use App\Models\ScreenViewAudit;
use App\Models\Setting;
use App\Models\StudentPcAssignment;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Carbon;

/**
 * The one place that says which tables the daily purge deletes from, by which date column
 * and under which retention class (DECISIONS #29, F109). A table that is neither listed here
 * nor in KEPT fails RetentionRegistryTest, so a new table cannot be forgotten.
 */
final class RetentionRegistry
{
    public const RAW = 'raw';        // setting retention_days_raw

    public const REPORT = 'report';  // setting retention_days_reports

    /**
     * table => [model, date column, class, file column or null].
     * `commands` holds the `message` text sent to students (args.text), so it ages out with raw data.
     *
     * @return array<string, array{0: class-string<Model>, 1: string, 2: string, 3: ?string}>
     */
    public static function entries(): array
    {
        return [
            'events' => [Event::class, 'occurred_at', self::RAW, null],
            'key_stats' => [KeyStat::class, 'window_start', self::RAW, null],
            'paste_events' => [PasteEvent::class, 'occurred_at', self::RAW, null],
            'net_events' => [NetEvent::class, 'occurred_at', self::RAW, null],
            'commands' => [Command::class, 'created_at', self::RAW, null],
            'evidence_captures' => [EvidenceCapture::class, 'occurred_at', self::RAW, 'file_path'],
            'alerts' => [Alert::class, 'occurred_at', self::REPORT, null],
            'scores' => [Score::class, 'created_at', self::REPORT, null],
            'screen_view_audits' => [ScreenViewAudit::class, 'started_at', self::REPORT, null],
            'student_pc_assignments' => [StudentPcAssignment::class, 'last_seen_at', self::REPORT, null],
            'reports' => [Report::class, 'created_at', self::REPORT, null],
        ];
    }

    /** Tables that are deliberately never purged here (config, people, audit trail, framework). */
    public const KEPT = [
        'agents', 'alert_reviews', 'audit_log', 'blocklist_entries', 'cache', 'cache_locks', 'consents',
        'enrollment_tokens', 'enrollments', 'failed_jobs', 'job_batches', 'jobs', 'lab_sessions', 'labs',
        'migrations', 'password_reset_tokens', 'pcs', 'policy_generation', 'profiles', 'resolved_policies', 'rules', 'seats',
        'session_pcs', 'sessions', 'settings', 'signature_versions', 'students', 'timetable_entries', 'users',
    ];

    public static function cutoff(string $class): Carbon
    {
        $key = $class === self::RAW ? 'retention_days_raw' : 'retention_days_reports';

        return now()->subDays((int) Setting::get($key));
    }
}
