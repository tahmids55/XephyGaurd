<?php

declare(strict_types=1);

namespace Tests\Feature;

use App\Jobs\PurgeRetention;
use App\Models\AuditLog;
use App\Models\EvidenceCapture;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\Profile;
use App\Models\ScreenViewAudit;
use App\Models\User;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Illuminate\Http\UploadedFile;
use Illuminate\Support\Facades\Crypt;
use Illuminate\Support\Facades\Storage;
use Tests\Concerns\BuildsLab;
use Tests\TestCase;

class EvidenceCaptureTest extends TestCase
{
    use BuildsLab, RefreshDatabase;

    private function tinyPng(): string
    {
        return base64_decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='
        );
    }

    private function openView(User $u, Pc $pc, array $extra = []): ScreenViewAudit
    {
        return ScreenViewAudit::create($extra + ['instructor_id' => $u->id, 'pc_id' => $pc->id, 'stream_id' => $pc->id,
            'mode' => 'live', 'started_at' => now()]);
    }

    public function test_instructor_can_upload_view_and_report_screen_evidence(): void
    {
        Storage::fake('local');
        $lab = $this->bootLab();
        [$pc] = $this->enrolledPc();
        $pc->update(['current_user' => 'alice', 'status' => 'normal', 'last_seen_at' => now()]);

        $instructor = $this->user('instructor');
        $profile = Profile::query()->where('mode', 'test')->firstOrFail();
        $session = LabSession::create([
            'lab_id' => $lab->id,
            'profile_id' => $profile->id,
            'started_by' => $instructor->id,
            'course' => 'CS202',
            'mode' => 'test',
            'status' => 'active',
            'starts_at' => now()->subMinutes(10),
        ]);

        $png = $this->tinyPng();
        $file = UploadedFile::fake()->createWithContent('snap.png', $png);

        $this->postJson(route('pcs.evidence', $pc), ['image' => $file])->assertUnauthorized();

        $res = $this->actingAs($instructor)
            ->post(route('pcs.evidence', $pc), [
                'image' => $file,
                'monitor' => 1,
                'source_mode' => 'live',
                'note' => 'Visible ChatGPT tab',
                'screen_view_id' => $this->openView($instructor, $pc, ['lab_session_id' => $session->id])->id,
            ], ['Accept' => 'application/json']);

        $res->assertCreated()
            ->assertJsonPath('ok', true)
            ->assertJsonPath('evidence.monitor', 1)
            ->assertJsonPath('evidence.source_mode', 'live')
            ->assertJsonPath('evidence.user', 'alice')
            ->assertJsonPath('evidence.sha256', hash('sha256', $png))
            ->assertJsonPath('evidence.size_bytes', strlen($png));

        $capture = EvidenceCapture::query()->firstOrFail();
        Storage::disk('local')->assertExists($capture->file_path);
        $stored = Storage::disk('local')->get($capture->file_path);
        $this->assertTrue($capture->encrypted);
        $this->assertStringNotContainsString(substr($png, 0, 8), $stored);   // no PNG header at rest
        $this->assertSame($png, Crypt::decryptString($stored));
        $this->assertDatabaseHas('audit_log', [
            'action' => 'pc.evidence',
            'subject_id' => $pc->id,
        ]);

        $this->actingAs($instructor)
            ->getJson(route('pcs.show', $pc))
            ->assertOk()
            ->assertJsonPath('evidence.0.id', $capture->id)
            ->assertJsonPath('evidence.0.sha256', hash('sha256', $png));

        $imgRes = $this->actingAs($instructor)->get(route('evidence.show', $capture));
        $imgRes->assertOk()->assertHeader('Content-Type', 'image/png');
        $this->assertSame($png, $imgRes->getContent());
        $this->assertDatabaseHas('audit_log', ['action' => 'evidence.view', 'subject_id' => $capture->id]);
        $this->assertSame($session->id, $capture->lab_session_id);

        $this->actingAs($instructor)
            ->get(route('reports.show', $session))
            ->assertOk()
            ->assertSee('Saved screen evidence (1)')
            ->assertSee(substr($capture->sha256, 0, 12));

        $this->actingAs($instructor)
            ->get(route('reports.pdf', $session))
            ->assertOk()
            ->assertHeader('content-type', 'application/pdf');
    }

    public function test_invalid_file_is_rejected_and_retention_purges_expired_evidence(): void
    {
        Storage::fake('local');
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $instructor = $this->user('instructor');
        $view = $this->openView($instructor, $pc);

        $bad = UploadedFile::fake()->createWithContent('notes.txt', 'not a png');
        $this->actingAs($instructor)
            ->post(route('pcs.evidence', $pc), ['image' => $bad, 'screen_view_id' => $view->id], ['Accept' => 'application/json'])
            ->assertUnprocessable();

        $png = $this->tinyPng();
        $good = UploadedFile::fake()->createWithContent('old.png', $png);
        $this->actingAs($instructor)
            ->post(route('pcs.evidence', $pc), ['image' => $good, 'screen_view_id' => $view->id], ['Accept' => 'application/json'])
            ->assertCreated();

        $capture = EvidenceCapture::query()->firstOrFail();
        $capture->forceFill(['occurred_at' => now()->subDays(200)])->save();
        Storage::disk('local')->assertExists($capture->file_path);

        (new PurgeRetention)->handle();

        Storage::disk('local')->assertMissing($capture->file_path);
        $this->assertDatabaseMissing('evidence_captures', ['id' => $capture->id]);
        $this->assertGreaterThan(0, AuditLog::query()->where('action', 'retention.purge')->count());
    }

    public function test_evidence_needs_instructor_and_own_open_view_of_that_pc(): void
    {
        Storage::fake('local');
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        [$other] = $this->enrolledPc('192.168.10.22', 'PC-02');
        $instructor = $this->user('instructor');
        $colleague = $this->user('instructor');
        $assistant = $this->user('assistant');
        $post = fn (User $u, Pc $p, $viewId) => $this->actingAs($u)->post(route('pcs.evidence', $p), [
            'image' => UploadedFile::fake()->createWithContent('s.png', $this->tinyPng()), 'screen_view_id' => $viewId,
        ], ['Accept' => 'application/json']);

        $view = $this->openView($instructor, $pc);
        $post($assistant, $pc, $view->id)->assertForbidden();
        $post($instructor, $pc, null)->assertUnprocessable();                              // no view
        $post($colleague, $pc, $view->id)->assertUnprocessable();                         // someone else's view
        $post($instructor, $other, $view->id)->assertUnprocessable();                     // view of another PC
        $stale = $this->openView($instructor, $pc);
        ScreenViewAudit::whereKey($stale->id)->update(['updated_at' => now()->subMinutes(5)]);
        $post($instructor, $pc, $stale->id)->assertUnprocessable();                       // lease lapsed
        $ended = $this->openView($instructor, $pc, ['ended_at' => now()]);
        $post($instructor, $pc, $ended->id)->assertUnprocessable();                       // stopped
        $this->assertSame(0, EvidenceCapture::count());

        $post($instructor, $pc, $view->id)->assertCreated();
        $capture = EvidenceCapture::firstOrFail();
        $this->assertSame($view->id, $capture->screen_view_audit_id);
        $this->actingAs($assistant)->get(route('evidence.show', $capture))->assertForbidden();
        $this->actingAs($assistant)->getJson(route('pcs.show', $pc))->assertOk()->assertJsonCount(0, 'evidence');
    }

    public function test_tampered_blob_is_refused_and_legacy_plain_file_still_served(): void
    {
        Storage::fake('local');
        $this->bootLab();
        [$pc] = $this->enrolledPc();
        $instructor = $this->user('instructor');
        $png = $this->tinyPng();
        $base = ['pc_id' => $pc->id, 'captured_by' => $instructor->id, 'mime_type' => 'image/png',
            'sha256' => hash('sha256', $png), 'size_bytes' => strlen($png), 'occurred_at' => now()];

        Storage::disk('local')->put('evidence/legacy.png', $png);
        $legacy = EvidenceCapture::create($base + ['file_path' => 'evidence/legacy.png', 'encrypted' => false]);
        $this->assertSame($png, $this->actingAs($instructor)->get(route('evidence.show', $legacy))->assertOk()->getContent());

        Storage::disk('local')->put('evidence/swapped.bin', Crypt::encryptString('other image'));
        $swapped = EvidenceCapture::create($base + ['file_path' => 'evidence/swapped.bin', 'encrypted' => true]);
        $this->actingAs($instructor)->get(route('evidence.show', $swapped))->assertStatus(409);
        $this->assertDatabaseMissing('audit_log', ['action' => 'evidence.view', 'subject_id' => $swapped->id]);
        Storage::disk('local')->put('evidence/swapped.bin', 'garbage');
        $this->actingAs($instructor)->get(route('evidence.show', $swapped))->assertStatus(409);
    }
}
