<?php

declare(strict_types=1);

namespace App\Http\Controllers\Agent;

use App\Http\Controllers\Controller;
use App\Models\Agent;
use App\Models\Alert;
use App\Models\Command;
use App\Models\EvidenceCapture;
use App\Services\Audit;
use Illuminate\Database\UniqueConstraintViolationException;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Crypt;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Storage;
use Illuminate\Support\Str;

/**
 * F123 / DECISIONS #32: the agent uploads the screenshot asked for by `evidence_snapshot`.
 * Raw JPEG body (no base64, fastest), `?nonce=<32 hex>`; `?masked=1` with an empty body when the
 * privacy mask hid the screen. Accepted only from the mTLS agent of the PC the command was sent to,
 * with that command's nonce, within NONCE_TTL_S, once (THREAT_MODEL #34).
 */
class EvidenceUploadController extends Controller
{
    public const NONCE_TTL_S = 60;

    public const MAX_BYTES = 8 * 1024 * 1024;

    public const MAX_DIM = 8192;

    public function store(Request $request): JsonResponse
    {
        /** @var Agent $agent */
        $agent = $request->attributes->get('agent');
        $pc = $agent->pc;
        $nonce = (string) $request->query('nonce', '');
        abort_unless(preg_match('/^[0-9a-f]{32}$/', $nonce) === 1, 422, 'bad nonce');

        $cmd = Command::query()->where('pc_id', $pc->id)->where('type', 'evidence_snapshot')
            ->where('created_at', '>=', now()->subSeconds(self::NONCE_TTL_S))->latest('id')->limit(10)->get()
            ->first(fn (Command $c) => hash_equals((string) ($c->args['nonce'] ?? ''), $nonce));
        abort_if($cmd === null, 403, 'unknown or expired nonce');
        $alert = Alert::query()->whereKey((int) ($cmd->args['alert_id'] ?? 0))->where('pc_id', $pc->id)->first();
        abort_if($alert === null, 403, 'unknown alert');
        abort_unless(Cache::add('evidence:nonce:'.$cmd->id, 1, self::NONCE_TTL_S * 2), 409, 'nonce already used');

        if ($request->boolean('masked')) {
            Audit::log('pc.evidence.auto_masked', $pc, ['alert_id' => $alert->id, 'command_id' => $cmd->id]);

            return response()->json(['ok' => true, 'masked' => true]);
        }

        $bytes = $request->getContent();
        $size = strlen($bytes);
        abort_if($size === 0, 422, 'empty image');
        abort_if($size > self::MAX_BYTES, 413);
        $info = @getimagesizefromstring($bytes);
        abort_unless(is_array($info) && $info[2] === IMAGETYPE_JPEG && $info[0] > 0 && $info[1] > 0
            && $info[0] <= self::MAX_DIM && $info[1] <= self::MAX_DIM, 422, 'not a JPEG image');

        $path = 'evidence/pc_'.$pc->id.'/'.Str::uuid().'.bin';
        Storage::disk('local')->put($path, Crypt::encryptString($bytes));   // as F46 (S-13)
        try {
            // Row and audit together: an unaudited capture is never kept (I-SERVER-14).
            $capture = DB::transaction(function () use ($pc, $alert, $cmd, $path, $bytes, $size) {
                $c = EvidenceCapture::create([
                    'pc_id' => $pc->id, 'lab_session_id' => $alert->lab_session_id, 'user' => $alert->user ?? $pc->current_user,
                    'captured_by' => null, 'screen_view_audit_id' => null, 'alert_id' => $alert->id, 'command_id' => $cmd->id,
                    'monitor' => 0, 'source_mode' => 'auto', 'file_path' => $path, 'mime_type' => 'image/jpeg',
                    'sha256' => hash('sha256', $bytes), 'size_bytes' => $size, 'encrypted' => true,
                    'note' => Str::limit('Auto on alert: '.$alert->title, 500, ''), 'occurred_at' => now(),
                ]);
                Audit::log('pc.evidence.auto', $pc, ['evidence_id' => $c->id, 'alert_id' => $alert->id,
                    'command_id' => $cmd->id, 'sha256' => $c->sha256, 'size_bytes' => $size]);

                return $c;
            });
        } catch (UniqueConstraintViolationException) {
            Storage::disk('local')->delete($path);
            abort(409, 'nonce already used');
        } catch (\Throwable $e) {
            Storage::disk('local')->delete($path);
            throw $e;
        }

        return response()->json(['ok' => true, 'id' => $capture->id], 201);
    }
}
