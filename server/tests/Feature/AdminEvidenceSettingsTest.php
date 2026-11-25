<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Models\Setting;
use App\Services\NoticeText;
use App\Services\PrivacySettings;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F123: admins switch the auto screenshot on/off and pick its minimum severity; every change is audited. */
class AdminEvidenceSettingsTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private const FORM = ['lab_name' => 'Lab 9', 'network_mode' => 'proxy_only', 'ccc_ip' => '10.1.1.2', 'lab_subnet' => '10.1.1.0/24',
        'retention_days_raw' => 30, 'retention_days_reports' => 365, 'notice_text' => 'n', 'paste_alert_chars' => 500,
        'title_capture_always_on' => 'off', 'title_capture_practice' => 'app_only', 'title_capture_test' => 'app_only'];

    private function changes()
    {
        return AuditLog::query()->where('action', 'evidence.settings.change')->orderBy('id')->get();
    }

    public function test_page_shows_the_toggle_off_with_high_by_default(): void
    {
        $this->bootLab();
        $html = $this->actingAs($this->user('admin'))->get('/settings')->assertOk()->getContent();
        $this->assertStringContainsString('name="evidence_auto_screenshot"', $html);
        $this->assertMatchesRegularExpression('/<option value="high"\s+selected/', $html);
        $this->assertDoesNotMatchRegularExpression('/name="evidence_auto_screenshot"[^>]*checked/', $html);
    }

    public function test_turning_on_and_off_saves_settings_and_audits_each_change_once(): void
    {
        $this->bootLab();
        $admin = $this->user('admin');
        $this->actingAs($admin);

        $this->post('/settings', self::FORM)->assertRedirect();             // unchanged default: no audit row
        $this->assertCount(0, $this->changes());

        $this->post('/settings', self::FORM + ['evidence_auto_screenshot' => 1, 'evidence_min_severity' => 'critical'])
            ->assertSessionHasNoErrors();
        $this->assertSame(['enabled' => true, 'min_severity' => 'critical'], PrivacySettings::autoScreenshot());
        $this->assertStringContainsString('automatically', NoticeText::text());
        $this->assertMatchesRegularExpression('/name="evidence_auto_screenshot"[^>]*checked/', $this->get('/settings')->getContent());

        $this->post('/settings', self::FORM + ['evidence_auto_screenshot' => 1, 'evidence_min_severity' => 'critical']);
        $this->assertCount(1, $this->changes());                             // same values again: no new row

        $this->post('/settings', self::FORM)->assertRedirect();             // box unchecked = off, severity kept
        $this->assertSame(['enabled' => false, 'min_severity' => 'critical'], PrivacySettings::autoScreenshot());
        $this->assertStringNotContainsString('automatically', NoticeText::text());

        $rows = $this->changes();
        $this->assertCount(2, $rows);
        $this->assertSame($admin->id, $rows[0]->user_id);
        $this->assertSame(['from' => ['enabled' => false, 'min_severity' => 'high'], 'to' => ['enabled' => true, 'min_severity' => 'critical']],
            $rows[0]->details);
        $this->assertSame(['enabled' => false, 'min_severity' => 'critical'], $rows[1]->details['to']);
    }

    public function test_invalid_severity_is_refused_and_only_admins_can_change_it(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('admin'))
            ->post('/settings', self::FORM + ['evidence_auto_screenshot' => 1, 'evidence_min_severity' => 'info'])
            ->assertSessionHasErrors('evidence_min_severity');
        $this->assertFalse(PrivacySettings::autoScreenshot()['enabled']);

        $this->actingAs($this->user('instructor'))
            ->post('/settings', self::FORM + ['evidence_auto_screenshot' => 1])->assertForbidden();
        $this->assertNull(Setting::get('evidence.auto_screenshot'));
        $this->assertCount(0, $this->changes());
    }
}
