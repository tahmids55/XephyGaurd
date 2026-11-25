<?php

declare(strict_types=1);

namespace App\Http\Controllers\Web;

use App\Http\Controllers\Controller;
use App\Models\EvidenceCapture;
use App\Models\LabSession;
use App\Models\Pc;
use App\Models\ScreenViewAudit;
use App\Services\Audit;
use Illuminate\Contracts\Encryption\DecryptException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Http\Response;
use Illuminate\Support\Facades\Crypt;
use Illuminate\Support\Facades\Storage;
use Illuminate\Support\Str;

/** Handles storing and streaming instructor-captured screenshot evidence. */
class EvidenceController extends Controller
{
    public function store(Request $request, Pc $pc): JsonResponse
    {
        $d = $request->validate([
            'image' => 'required|file|mimes:png,webp,jpeg,jpg|max:8192',
            'monitor' => 'nullable|integer|min:0|max:15',
            'source_mode' => 'nullable|string|in:snapshot,live',
            'note' => 'nullable|string|max:500',
            'screen_view_id' => 'required|integer|min:1',
        ]);
        $view = $this->openView($request, $pc, (int) $d['screen_view_id']);
        if (! $view) {
            return response()->json(['message' => 'Evidence can only be saved from your own open live view of this PC.'], 422);
        }

        $file = $request->file('image');
        $bytes = file_get_contents($file->getRealPath());
        if ($bytes === false || $bytes === '') {
            return response()->json(['message' => 'Uploaded image is empty.'], 422);
        }

        $ext = strtolower($file->guessExtension() ?: $file->extension() ?: 'png');
        $ext = match ($ext) {
            'jpeg', 'jpg' => 'jpg',
            'webp' => 'webp',
            default => 'png',
        };
        $mime = match ($ext) {
            'jpg' => 'image/jpeg',
            'webp' => 'image/webp',
            default => 'image/png',
        };

        $sha256 = hash('sha256', $bytes);
        $size = strlen($bytes);
        $path = 'evidence/pc_'.$pc->id.'/'.Str::uuid().'.bin';

        // At rest only the APP_KEY-encrypted blob is kept; sha256 of the plain image is in the row (S-13).
        Storage::disk('local')->put($path, Crypt::encryptString($bytes));

        $session = $view->lab_session_id ? null : LabSession::query()->where('status', 'active')->first();
        $capture = EvidenceCapture::create([
            'pc_id' => $pc->id,
            'lab_session_id' => $view->lab_session_id ?? $session?->id,
            'user' => $pc->current_user,
            'captured_by' => $request->user()?->id,
            'screen_view_audit_id' => $view->id,
            'monitor' => (int) ($d['monitor'] ?? 0),
            'source_mode' => $d['source_mode'] ?? 'snapshot',
            'file_path' => $path,
            'mime_type' => $mime,
            'sha256' => $sha256,
            'size_bytes' => $size,
            'encrypted' => true,
            'note' => $d['note'] ?? null,
            'occurred_at' => now(),
        ]);
        $capture->load('capturer:id,name');

        Audit::log('pc.evidence', $pc, [
            'evidence_id' => $capture->id,
            'screen_view_id' => $view->id,
            'monitor' => $capture->monitor,
            'source_mode' => $capture->source_mode,
            'sha256' => $sha256,
            'size_bytes' => $size,
        ]);

        return response()->json([
            'ok' => true,
            'evidence' => $capture->toApiArray(),
        ], 201);
    }

    public function show(EvidenceCapture $capture): Response
    {
        $disk = Storage::disk('local');
        if (! $disk->exists($capture->file_path)) {
            abort(404);
        }
        $raw = (string) $disk->get($capture->file_path);
        try {
            $bytes = $capture->encrypted ? Crypt::decryptString($raw) : $raw;
        } catch (DecryptException) {
            abort(409, 'Evidence file cannot be decrypted (changed, or APP_KEY rotated).');
        }
        // Integrity: the stored hash is of the plain image; a changed blob is never served.
        if (! hash_equals((string) $capture->sha256, hash('sha256', $bytes))) {
            abort(409, 'Evidence file does not match its recorded hash.');
        }
        Audit::log('evidence.view', $capture, ['evidence_id' => $capture->id, 'pc_id' => $capture->pc_id]);

        return response($bytes, 200, [
            'Content-Type' => $capture->mime_type,
            'Content-Disposition' => 'inline; filename="evidence-'.$capture->id.'"',
            'X-Content-Type-Options' => 'nosniff',
            'Cache-Control' => 'private, no-store',
        ]);
    }

    /**
     * The caller's own screen view of this PC that is still open: not stopped and renewed within the
     * lease window (the viewer renews every lease, see ViewTokenController::renewToken).
     */
    private function openView(Request $request, Pc $pc, int $id): ?ScreenViewAudit
    {
        $ttl = max(5, (int) config('services.screen_gateway.ttl', 30));

        return ScreenViewAudit::query()
            ->whereKey($id)
            ->where('instructor_id', $request->user()?->id)
            ->where('pc_id', $pc->id)
            ->whereNull('ended_at')
            ->where('updated_at', '>=', now()->subSeconds(2 * $ttl + 10))
            ->first();
    }
}
