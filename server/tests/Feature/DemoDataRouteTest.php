<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\User;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\TestCase;

class DemoDataRouteTest extends TestCase
{
    use RefreshDatabase;

    protected function setUp(): void
    {
        parent::setUp();
        User::factory()->create(['role' => 'admin']);
    }

    public function test_demo_data_is_not_served_outside_local(): void
    {
        foreach (['testing', 'production'] as $env) {
            $this->app['env'] = $env;
            $this->get('/js/data.js')->assertNotFound();
        }
        $this->assertFileDoesNotExist(public_path('js/data.js'));
    }

    public function test_demo_data_is_served_in_local(): void
    {
        $this->app['env'] = 'local';

        $response = $this->get('/js/data.js')
            ->assertOk()
            ->assertHeader('Content-Type', 'application/javascript');

        $this->assertStringContainsString('window.XG', $response->baseResponse->getFile()->getContent());
    }
}
