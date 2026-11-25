<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Profile;
use App\Models\Setting;
use App\Services\PolicyService;
use App\Services\PrivacySettings;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class PrivacySettingsTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_defaults_are_safe_and_invalid_values_count_as_off(): void
    {
        $this->assertSame('off', PrivacySettings::titleCaptureSetting('always_on'));
        $this->assertSame('app_only', PrivacySettings::titleCaptureSetting('practice'));
        $this->assertSame('app_only', PrivacySettings::titleCaptureSetting('test'));
        $this->assertSame('off', PrivacySettings::titleCaptureSetting('unknown_mode'));
        Setting::put('privacy.title_capture.test', 'everything');
        $this->assertSame('off', PrivacySettings::titleCaptureSetting('test'));
        $this->assertSame(['enabled' => false, 'query' => false], PrivacySettings::logUrls());
        Setting::put('privacy.log_urls', ['enabled' => false, 'query' => true]);
        $this->assertFalse(PrivacySettings::logUrls()['query']);              // queries only with URLs on
    }

    public function test_effective_title_capture_is_the_stricter_of_profile_and_setting(): void
    {
        foreach ([['full', 'off', 'off'], ['off', 'full', 'off'], ['full', 'app_only', 'app_only'],
            ['app_only', 'full', 'app_only'], ['full', 'full', 'full'], [null, 'full', 'full'], ['bogus', 'full', 'off']] as [$profile, $setting, $want]) {
            Setting::put('privacy.title_capture.test', $setting);
            $this->assertSame($want, PrivacySettings::effectiveTitleCapture('test', $profile), json_encode([$profile, $setting]));
        }
    }

    public function test_policy_sends_the_stricter_value(): void
    {
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $always = PolicyService::alwaysOnProfile();
        $always->update(['agent' => ['title_capture' => 'full'] + ($always->agent ?? [])]);
        $this->assertSame('off', app(PolicyService::class)->forPc($pc)['title_capture']);   // setting default off wins
        Setting::put('privacy.title_capture.always_on', 'full');
        $this->assertSame('full', app(PolicyService::class)->forPc($pc)['title_capture']);
        $always->update(['agent' => ['title_capture' => 'app_only'] + ($always->agent ?? [])]);
        $this->assertSame('app_only', app(PolicyService::class)->forPc($pc)['title_capture']); // profile stricter wins
        $this->assertInstanceOf(Profile::class, $always);
    }
}
