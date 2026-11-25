<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\AlertReview;
use App\Models\AuditLog;
use App\Models\Lab;
use App\Models\Pc;
use Illuminate\Foundation\Testing\RefreshDatabase;
use LogicException;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-31 (DECISIONS #29): acknowledging appends an alert_reviews row, once, and batch ids stay in the lab. */
class AlertReviewTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function alert(Pc $pc, string $rule = 'r'): Alert
    {
        return Alert::create(['pc_id' => $pc->id, 'rule_id' => $rule, 'severity' => 'high', 'title' => $rule, 'occurred_at' => now()]);
    }

    public function test_ack_appends_one_review_and_a_second_ack_changes_nothing(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $a = $this->alert($pc);
        $first = $this->user('assistant');
        $this->actingAs($first)->postJson("/alerts/{$a->id}/ack", ['comment' => 'first'])->assertOk()->assertJson(['already' => false]);

        $this->actingAs($this->user('admin'))->postJson("/alerts/{$a->id}/ack", ['comment' => 'second'])
            ->assertOk()->assertJson(['already' => true]);

        $this->assertSame(1, AlertReview::count());
        $r = AlertReview::first();
        $this->assertSame([$a->id, $first->id, 'first'], [$r->alert_id, $r->user_id, $r->note]);
        $this->assertSame($first->id, $a->fresh()->acknowledged_by);
        $this->assertSame('first', $a->fresh()->comment);
        $this->assertSame(1, AuditLog::where('action', 'alert.ack')->count());
    }

    public function test_batch_skips_acknowledged_and_other_lab_alerts(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $mine = $this->alert($pc, 'mine');
        $done = $this->alert($pc, 'done');
        $done->acknowledgeBy($this->user('admin')->id, 'earlier');
        $otherLab = Lab::create(['name' => 'Other lab']);
        $foreignPc = Pc::create(['lab_id' => $otherLab->id, 'name' => 'X-01', 'hostname' => 'x01', 'ip' => '10.9.9.9',
            'os' => 'Linux', 'macs' => ['aa:bb:cc:00:00:99']]);
        $foreign = $this->alert($foreignPc, 'foreign');

        $this->actingAs($this->user('assistant'))
            ->postJson('/alerts/ack-batch', ['ids' => [$mine->id, $done->id, $foreign->id]])->assertOk();

        $this->assertNotNull($mine->fresh()->acknowledged_at);
        $this->assertNull($foreign->fresh()->acknowledged_at);
        $this->assertSame('earlier', $done->fresh()->comment);
        $this->assertEqualsCanonicalizing([$mine->id, $done->id], AlertReview::pluck('alert_id')->all());
        $this->assertSame(1, AuditLog::where('action', 'alert.ack')->where('subject_id', $mine->id)->count());
    }

    public function test_reviews_are_append_only(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $a = $this->alert($pc);
        $a->acknowledgeBy($this->user('admin')->id, null);
        $r = AlertReview::first();

        $this->expectException(LogicException::class);
        $r->delete();
    }
}
