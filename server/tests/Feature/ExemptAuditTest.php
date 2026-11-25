<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Models\Student;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ExemptAuditTest extends TestCase
{
    use BuildsLab;
    use RefreshDatabase;

    public function test_audit_records_the_exemption_change_but_not_the_reason(): void
    {
        $this->bootLab();
        $st = Student::create(['student_number' => '5', 'name' => 'E', 'login' => 'e']);
        $this->actingAs($this->user('instructor'));

        $this->postJson("/roster/{$st->id}/exempt", ['exempt' => 1, 'reason' => 'Medical condition'])->assertOk();
        $this->postJson("/roster/{$st->id}/exempt", ['exempt' => 0])->assertOk();

        $rows = AuditLog::where('action', 'student.exempt')->orderBy('id')->get();
        $this->assertCount(2, $rows);
        $this->assertSame(['exempt' => true], $rows[0]->details);
        $this->assertSame(['exempt' => false], $rows[1]->details);
        $this->assertStringNotContainsString('Medical', json_encode($rows->pluck('details')->all()));
    }
}
