<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Setting;
use App\Services\NoticeText;
use App\Services\PolicyService;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-25: the screen part of the student notice follows the enabled features, for the agent and /notice. */
class NoticeTextTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_custom_text_still_states_live_view_and_screenshots(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        config(['services.screen_gateway.secret' => 'test-secret']);
        Setting::put('notice_text', 'We only watch for AI tools.');
        $notice = app(PolicyService::class)->forPc($pc)['notice_text'];
        $this->assertStringStartsWith('We only watch for AI tools.', $notice);
        $this->assertStringContainsString('view this screen live', $notice);
        $this->assertStringContainsString('save a screenshot as evidence', $notice);
        $this->get('/notice')->assertOk()->assertSee('We only watch for AI tools.')
            ->assertSee('save a screenshot as evidence')->assertSee('and save a still as evidence');
    }

    public function test_old_default_text_is_not_stated_twice(): void
    {
        $this->bootLab();
        config(['services.screen_gateway.secret' => 'test-secret']);
        Setting::put('notice_text', NoticeText::BASE.' Lab staff can view this screen live and may save a screenshot as evidence; every view is logged.');
        $this->assertSame(1, substr_count(NoticeText::text(), 'view this screen live'));
        $this->assertStringStartsWith(NoticeText::BASE, NoticeText::text());
        Setting::put('notice_text', '');
        $this->assertStringStartsWith(NoticeText::BASE, NoticeText::text());
    }

    public function test_without_live_view_the_notice_says_so_and_the_policy_changes(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $svc = app(PolicyService::class);
        config(['services.screen_gateway.secret' => 'test-secret']);
        $on = $svc->forPc($pc);
        config(['services.screen_gateway.secret' => '']);
        $off = $svc->forPc($pc);
        $this->assertNotSame($on['version'], $off['version']);   // cache stamp covers the features
        $this->assertStringContainsString('Live screen viewing is turned off', $off['notice_text']);
        $this->assertStringNotContainsString('screenshot', $off['notice_text']);
        $this->get('/notice')->assertOk()->assertSee('Live screen viewing is turned off')
            ->assertDontSee('save a still as evidence');
    }
}
