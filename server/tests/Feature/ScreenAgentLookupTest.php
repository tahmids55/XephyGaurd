<?php

namespace Tests\Feature;

use App\Models\Agent;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class ScreenAgentLookupTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function lookup(string $fp, ?string $token = 'internal-test-token')
    {
        $headers = $token === null ? [] : ['Authorization' => "Bearer $token"];

        return $this->getJson("/api/internal/screen/agent/{$fp}", $headers);
    }

    public function test_known_agent_fingerprint_returns_its_pc(): void
    {
        $this->bootLab();
        config(['xephyguard.internal_token' => 'internal-test-token']);
        [$pc, , $pem] = $this->enrolledPc('192.168.10.51', 'PC-SCR');

        $this->lookup(openssl_x509_fingerprint($pem, 'sha256'))->assertOk()->assertExactJson(['pc_id' => $pc->id]);
    }

    public function test_requires_internal_token(): void
    {
        $this->bootLab();
        config(['xephyguard.internal_token' => 'internal-test-token']);
        [, , $pem] = $this->enrolledPc('192.168.10.52', 'PC-SCR2');
        $fp = openssl_x509_fingerprint($pem, 'sha256');

        $this->lookup($fp, null)->assertUnauthorized();
        $this->lookup($fp, 'wrong-token')->assertUnauthorized();
    }

    public function test_unknown_revoked_or_retired_agent_is_not_found(): void
    {
        $this->bootLab();
        config(['xephyguard.internal_token' => 'internal-test-token']);
        [$pc, , $pem] = $this->enrolledPc('192.168.10.53', 'PC-SCR3');
        $fp = openssl_x509_fingerprint($pem, 'sha256');

        $this->lookup(str_repeat('0', 64))->assertNotFound();

        Agent::where('pc_id', $pc->id)->update(['revoked_at' => now()]);
        $this->lookup($fp)->assertNotFound();

        Agent::where('pc_id', $pc->id)->update(['revoked_at' => null]);
        $pc->update(['retired_at' => now()]);
        $this->lookup($fp)->assertNotFound();
    }

    public function test_malformed_fingerprint_is_not_routed(): void
    {
        $this->bootLab();
        config(['xephyguard.internal_token' => 'internal-test-token']);

        $this->lookup('ABC')->assertNotFound();
        $this->lookup(str_repeat('g', 64))->assertNotFound();
    }
}
