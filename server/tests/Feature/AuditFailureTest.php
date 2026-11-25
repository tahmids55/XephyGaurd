<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\AuditLog;
use App\Services\Audit;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Support\Facades\Schema;
use Tests\TestCase;

class AuditFailureTest extends TestCase
{
    use RefreshDatabase;

    public function test_log_writes_a_row(): void
    {
        $row = Audit::log('test.action', null, ['k' => 'v']);

        $this->assertTrue($row->exists);
        $this->assertSame(1, AuditLog::where('action', 'test.action')->count());
    }

    public function test_log_throws_when_the_write_fails(): void
    {
        Schema::drop('audit_log');

        $this->expectException(\Throwable::class);
        Audit::log('test.action');
    }
}
