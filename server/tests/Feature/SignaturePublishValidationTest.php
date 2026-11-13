<?php

namespace Tests\Feature;

use App\Models\BlocklistEntry;
use App\Models\SignatureVersion;
use App\Services\SignatureCompiler;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class SignaturePublishValidationTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_bootstrap_publishes_bundle_with_ai_and_ide_domains(): void
    {
        $this->bootLab();
        $bundle = json_decode(SignatureVersion::active()->bundle, true);

        $this->assertNotEmpty($bundle['domains']['ai']);
        $this->assertNotEmpty($bundle['domains']['ide']);
    }

    public function test_publish_refuses_bundle_without_domains_and_keeps_active_version(): void
    {
        $this->bootLab();
        $before = SignatureVersion::active()->version;
        BlocklistEntry::query()->whereIn('category', ['ai', 'ide'])->delete();

        try {
            app(SignatureCompiler::class)->publish(null, 'test');
            $this->fail('publish() must refuse a bundle without AI/IDE domains');
        } catch (\RuntimeException $e) {
            $this->assertStringContainsString('domains.ai', $e->getMessage());
            $this->assertStringContainsString('domains.ide', $e->getMessage());
        }

        $this->assertSame($before, SignatureVersion::active()->version);
    }

    public function test_publish_is_noop_when_nothing_changed(): void
    {
        $this->bootLab();
        $first = SignatureVersion::active();

        $this->assertSame($first->version, app(SignatureCompiler::class)->publish(null, 'again')->version);
    }
}
