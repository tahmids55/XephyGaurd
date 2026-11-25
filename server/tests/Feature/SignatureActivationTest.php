<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\ResolvedPolicy;
use App\Models\Rule;
use App\Models\SignatureVersion;
use App\Services\CertificateAuthority;
use App\Services\SignatureCompiler;
use Illuminate\Foundation\Testing\RefreshDatabase;
use RuntimeException;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** S-48 (I-SERVER-18): sign first, switch the active version in one locked transaction. */
class SignatureActivationTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_signing_failure_keeps_the_active_version(): void
    {
        $this->bootLab();
        $before = SignatureVersion::active();
        $count = SignatureVersion::query()->count();
        Rule::query()->create(['rule_id' => 'proc.signfail', 'kind' => 'process', 'severity' => 'high',
            'definition' => ['match' => ['exe' => ['signfail']]], 'enabled' => true]);
        $dir = app(CertificateAuthority::class)->dir();
        $this->app->instance(CertificateAuthority::class, new class($dir) extends CertificateAuthority
        {
            public function signData(string $data): string
            {
                throw new RuntimeException('signing failed');
            }
        });

        try {
            app(SignatureCompiler::class)->publish(null, 'test');
            $this->fail('publish() must fail when signing fails');
        } catch (RuntimeException $e) {
            $this->assertSame('signing failed', $e->getMessage());
        }

        $this->assertSame($before->id, SignatureVersion::active()?->id);
        $this->assertSame(1, SignatureVersion::query()->where('active', true)->count());
        $this->assertSame($count, SignatureVersion::query()->count());
    }

    public function test_activate_switches_to_exactly_one_version_and_invalidates_policies(): void
    {
        $this->bootLab();
        $first = SignatureVersion::active();
        Rule::query()->create(['rule_id' => 'proc.second', 'kind' => 'process', 'severity' => 'high',
            'definition' => ['match' => ['exe' => ['second']]], 'enabled' => true]);
        $second = app(SignatureCompiler::class)->publish(null, 'second');
        $this->assertNotSame($first->id, $second->id);
        // Legacy state: two rows marked active. Activation must leave only the chosen one.
        SignatureVersion::query()->whereKey($first->id)->update(['active' => true]);
        $generation = ResolvedPolicy::generation();

        $this->actingAs($this->user('admin'));
        $this->post("/signatures/{$first->id}/activate")->assertRedirect()->assertSessionHas('status');

        $this->assertSame([$first->id], SignatureVersion::query()->where('active', true)->pluck('id')->all());
        $this->assertGreaterThan($generation, ResolvedPolicy::generation());
        $this->assertDatabaseHas('audit_log', ['action' => 'signatures.rollback', 'subject_id' => $first->id]);
    }

    public function test_activate_refuses_a_version_whose_signature_does_not_verify(): void
    {
        $this->bootLab();
        $active = SignatureVersion::active();
        $forged = SignatureVersion::create(['version' => 'forged', 'bundle' => $active->bundle,
            'signature' => base64_encode('not a signature'), 'sha256' => $active->sha256, 'active' => false]);

        $this->actingAs($this->user('admin'));
        $this->post("/signatures/{$forged->id}/activate")->assertRedirect()->assertSessionHas('error');

        $this->assertSame([$active->id], SignatureVersion::query()->where('active', true)->pluck('id')->all());
        $this->assertDatabaseMissing('audit_log', ['action' => 'signatures.rollback']);
    }
}
