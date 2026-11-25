<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Jobs\PurgeRetention;
use App\Models\Alert;
use App\Models\Command;
use App\Models\Lab;
use App\Models\LabSession;
use App\Models\Profile;
use App\Models\Score;
use App\Models\ScreenViewAudit;
use App\Models\Setting;
use App\Models\Student;
use App\Models\StudentPcAssignment;
use App\Models\User;
use App\Services\RetentionRegistry;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Schema;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class RetentionRegistryTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_every_table_is_purged_or_explicitly_kept(): void
    {
        $tables = array_map(fn ($t) => preg_replace('/^.*\./', '', $t), Schema::getTableListing());
        $known = array_merge(array_keys(RetentionRegistry::entries()), RetentionRegistry::KEPT);
        $this->assertSame([], array_values(array_diff($tables, $known)), 'table missing from RetentionRegistry entries/KEPT');
        $this->assertSame([], array_values(array_intersect(array_keys(RetentionRegistry::entries()), RetentionRegistry::KEPT)));
    }

    public function test_purge_uses_raw_retention_for_commands_and_report_retention_for_the_rest(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        Setting::put('retention_days_raw', 30);
        Setting::put('retention_days_reports', 365);
        $lab = Lab::current();
        $session = LabSession::create(['lab_id' => $lab->id, 'profile_id' => Profile::query()->firstOrFail()->id,
            'course' => 'CS', 'mode' => 'practice', 'status' => 'ended', 'starts_at' => now()->subDays(500)]);
        $student = Student::create(['student_number' => 'S1', 'name' => 'A', 'login' => 'a']);
        $user = User::factory()->create();

        $cmd = fn (int $days) => tap(Command::create(['pc_id' => $pc->id, 'type' => 'message', 'args' => ['text' => 'hi'], 'status' => 'ok']),
            fn ($c) => DB::table('commands')->where('id', $c->id)->update(['created_at' => now()->subDays($days)]));
        $cmd(40);
        $cmd(5);

        $alert = fn (int $days) => Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'low', 'title' => 't', 'occurred_at' => now()->subDays($days)]);
        $alert(400);
        $alert(200);

        $score = fn (string $user, int $days) => tap(Score::create(['lab_session_id' => $session->id, 'pc_id' => $pc->id, 'user' => $user,
            'method' => 'v1', 'score' => 10, 'reasons' => []]), fn ($s) => DB::table('scores')->where('id', $s->id)->update(['created_at' => now()->subDays($days)]));
        $score('old', 400);
        $score('new', 200);

        $view = fn (int $days) => ScreenViewAudit::create(['instructor_id' => $user->id, 'student_id' => $student->id, 'pc_id' => $pc->id,
            'started_at' => now()->subDays($days)]);
        $view(400);
        $view(200);

        $assign = fn (int $days) => StudentPcAssignment::create(['student_id' => $student->id, 'pc_id' => $pc->id,
            'first_seen_at' => now()->subDays($days + 1), 'last_seen_at' => now()->subDays($days)]);
        $assign(400);
        $assign(200);

        (new PurgeRetention)->handle();

        $this->assertSame([5], Command::query()->get()->map(fn ($c) => (int) $c->created_at->diffInDays(now(), true))->all());
        $this->assertSame(1, Alert::count());
        $this->assertSame('new', Score::first()->user);
        $this->assertSame(1, ScreenViewAudit::count());
        $this->assertSame(1, StudentPcAssignment::count());
    }
}
