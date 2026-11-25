<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Models\Setting;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

/** F102 / S-8: the shared clipboard is instructor-only and labelled with the real user name. */
class ShareAccessTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    public function test_guest_cannot_read_or_write_the_shared_clipboard(): void
    {
        $this->bootLab();
        Setting::put('shared_clipboard', 'secret answer');

        $this->get('/share')->assertRedirect('/login');
        $this->get('/share.html')->assertRedirect('/login');
        $this->getJson('/share/raw')->assertUnauthorized();
        $this->postJson('/share', ['text' => 'from a lab seat'])->assertUnauthorized();

        $this->assertSame('secret answer', Setting::get('shared_clipboard'));
    }

    public function test_assistant_is_forbidden(): void
    {
        $this->bootLab();
        $this->actingAs($this->user('assistant'));

        $this->get('/share')->assertForbidden();
        $this->getJson('/share/raw')->assertForbidden();
        $this->postJson('/share', ['text' => 'x'])->assertForbidden();
        $this->assertStringNotContainsString('Shared clipboard', $this->get('/')->getContent());
    }

    public function test_inactive_instructor_is_forbidden(): void
    {
        $this->bootLab();
        $user = $this->user('instructor');
        $user->forceFill(['active' => false])->save();
        $this->actingAs($user);

        $this->postJson('/share', ['text' => 'x'])->assertForbidden();
    }

    public function test_instructor_post_is_labelled_with_real_name(): void
    {
        $this->bootLab();
        $user = $this->user('instructor');
        $user->forceFill(['name' => 'Dr. Rahman'])->save();
        $this->actingAs($user);

        $res = $this->postJson('/share', ['text' => 'ssh lab@10.0.0.1'])->assertOk()->json();
        $this->assertStringEndsWith('by Dr. Rahman', $res['updated_at']);
        $this->assertStringNotContainsString('Instructor', $res['updated_at']);

        $raw = $this->getJson('/share/raw')->assertOk()->json();
        $this->assertSame('ssh lab@10.0.0.1', $raw['text']);
        $this->assertStringEndsWith('by Dr. Rahman', $raw['updated_at']);
        $this->get('/share')->assertOk()->assertSee('Shared clipboard');
    }
}
