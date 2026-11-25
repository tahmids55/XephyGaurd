<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Alert;
use App\Models\Pc;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-33: the layout gets its open-alert count from a view composer; output is unchanged. */
class OpenAlertsComposerTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function alert(Pc $pc, ?string $ackAt = null): Alert
    {
        return Alert::create(['pc_id' => $pc->id, 'rule_id' => 'r', 'severity' => 'high', 'title' => 'r', 'occurred_at' => now(), 'acknowledged_at' => $ackAt]);
    }

    public function test_badge_shows_only_unacknowledged_alerts(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $this->alert($pc);
        $this->alert($pc);
        $this->alert($pc, now()->toDateTimeString());

        $this->actingAs($this->user('assistant'))->get('/alerts')
            ->assertOk()->assertSee('id="xg-open-alerts-badge">2<', false);
    }

    public function test_no_badge_when_nothing_is_open(): void
    {
        $this->bootLab();

        $this->actingAs($this->user('assistant'))->get('/alerts')
            ->assertOk()->assertDontSee('xg-open-alerts-badge', false);
    }
}
