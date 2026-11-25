<?php

declare(strict_types=1);

namespace App\Http\Controllers\Api;

use App\Http\Controllers\Controller;
use App\Models\Pc;
use App\Models\ScreenViewAudit;
use App\Models\SessionPc;
use App\Models\Student;
use App\Services\CommandBus;
use App\Services\ScreenGatewayRevoker;
use Firebase\JWT\JWT;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Symfony\Component\HttpFoundation\Response;

class ViewTokenController extends Controller
{
    /**
     * Issues a short-lived (~30s) JWT token for viewing a student's screen.
     * Enforces that the requesting instructor owns the active lab session.
     */
    public function issueToken(Request $request, Student $student): JsonResponse
    {
        $user = $request->user();
        if (! $user) {
            abort(Response::HTTP_UNAUTHORIZED, 'Authentication required');
        }

        // Locate active session for this student
        $sessionPc = SessionPc::where('student_id', $student->id)
            ->whereHas('session', function ($q) {
                $q->active();
            })
            ->with('session')
            ->first();

        $session = $sessionPc?->session;

        if (! $session || ! $sessionPc->pc_id) {
            return response()->json([
                'message' => 'No active lab session found for this student',
            ], Response::HTTP_NOT_FOUND);
        }

        // Verify the instructor owns the session (admins are allowed)
        if ($session->started_by !== $user->id && ! $user->hasRole('admin')) {
            return response()->json([
                'message' => 'Forbidden: You do not own this lab session',
            ], Response::HTTP_FORBIDDEN);
        }

        $validated = $request->validate([
            'mode' => 'nullable|string|in:live,snapshot,wall',
        ]);

        $mode = $validated['mode'] ?? 'live';
        // The stream is the PC's id (the agent streams as its PC). Never taken from the request.
        $pcId = (int) $sessionPc->pc_id;
        $streamId = $pcId;

        $this->requireSecret();
        $audit = ScreenViewAudit::create([
            'instructor_id' => $user->id,
            'student_id' => $student->id,
            'pc_id' => $pcId,
            'lab_session_id' => $session->id,
            'stream_id' => $streamId,
            'mode' => $mode,
            'started_at' => now(),
        ]);
        [$token, $exp] = $this->leaseToken($audit);
        $cfg = config('services.screen_gateway');

        return response()->json([
            'token' => $token,
            'stream_id' => $streamId,
            'audit_id' => $audit->id,
            'expires_at' => $exp,
            'gateway_url' => $this->resolveGatewayUrl($request, $cfg),
            'mode' => $mode,
        ]);
    }

    /**
     * Issues a short-lived (~30s) JWT token for viewing a PC's screen directly.
     * Works both during an active session and for ad-hoc / lab supervision.
     */
    public function issuePcToken(Request $request, Pc $pc): JsonResponse
    {
        $user = $request->user();
        if (! $user) {
            abort(Response::HTTP_UNAUTHORIZED, 'Authentication required');
        }

        // Check if there is an active session on this PC
        $sessionPc = SessionPc::where('pc_id', $pc->id)
            ->whereHas('session', function ($q) {
                $q->active();
            })
            ->with(['session', 'student'])
            ->first();

        $session = $sessionPc?->session;
        $student = $sessionPc?->student;

        // If in an active session, verify instructor owns the session or is admin
        if ($session && $session->started_by !== $user->id && ! $user->hasRole('admin')) {
            return response()->json([
                'message' => 'Forbidden: You do not own this lab session',
            ], Response::HTTP_FORBIDDEN);
        }

        $validated = $request->validate([
            'mode' => 'nullable|string|in:live,snapshot,wall',
        ]);

        $mode = $validated['mode'] ?? 'live';
        // The stream is the PC's id. Never taken from the request.
        $streamId = (int) $pc->id;

        $this->requireSecret();
        $audit = ScreenViewAudit::create([
            'instructor_id' => $user->id,
            'student_id' => $student?->id,
            'pc_id' => $pc->id,
            'lab_session_id' => $session?->id,
            'stream_id' => $streamId,
            'mode' => $mode,
            'started_at' => now(),
        ]);
        [$token, $exp] = $this->leaseToken($audit);
        $cfg = config('services.screen_gateway');

        return response()->json([
            'token' => $token,
            'stream_id' => $streamId,
            'audit_id' => $audit->id,
            'expires_at' => $exp,
            'gateway_url' => $this->resolveGatewayUrl($request, $cfg),
            'mode' => $mode,
        ]);
    }

    /**
     * Renews the short-lived lease for an active stream view.
     */
    public function renewToken(Request $request, ScreenViewAudit $audit): JsonResponse
    {
        $user = $request->user();
        if (! $user) {
            abort(Response::HTTP_UNAUTHORIZED);
        }

        if ($audit->instructor_id !== $user->id && ! $user->hasRole('admin')) {
            abort(Response::HTTP_FORBIDDEN, 'Unauthorized access to this stream audit');
        }

        if ($audit->ended_at !== null) {
            return response()->json([
                'message' => 'Stream has already ended',
            ], Response::HTTP_UNPROCESSABLE_ENTITY);
        }

        $session = $audit->labSession;
        if ($session && $session->status !== 'active') {
            $audit->update(['ended_at' => now()]);

            return response()->json([
                'message' => 'Lab session has ended',
            ], Response::HTTP_GONE);
        }

        // Every lease names its PC; audits from before pc_id was recorded cannot be renewed.
        if (! $audit->pc_id) {
            return response()->json([
                'message' => 'Stream has no PC; open the view again',
            ], Response::HTTP_UNPROCESSABLE_ENTITY);
        }

        $audit->touch(); // marks the view as still open (evidence needs an open view, S-12)
        [$token, $exp] = $this->leaseToken($audit);

        return response()->json([
            'token' => $token,
            'expires_at' => $exp,
        ]);
    }

    /**
     * Closes an active screen view and records ended_at in audit.
     */
    public function stopView(Request $request, ScreenViewAudit $audit): JsonResponse
    {
        $user = $request->user();
        if (! $user) {
            abort(Response::HTTP_UNAUTHORIZED);
        }

        if ($audit->instructor_id !== $user->id && ! $user->hasRole('admin')) {
            abort(Response::HTTP_FORBIDDEN);
        }

        if ($audit->ended_at === null) {
            $audit->update(['ended_at' => now()]);
        }
        app(ScreenGatewayRevoker::class)->audit($audit->id);

        return response()->json([
            'status' => 'stopped',
            'audit_id' => $audit->id,
            'duration_s' => (int) $audit->started_at->diffInSeconds($audit->ended_at),
        ]);
    }

    /**
     * Signs a lease for the audited view (docs/PROTOCOL.md "Screen view lease"). `audit_id`, `sub` (the
     * user who opened the view) and `lab_session_id` let the gateway revoke it; a renewal keeps all three.
     *
     * @return array{0: string, 1: int} token and expiry (unix time)
     */
    private function leaseToken(ScreenViewAudit $audit): array
    {
        $cfg = config('services.screen_gateway');
        $now = time();
        $exp = $now + max(5, (int) ($cfg['ttl'] ?? 30));

        // The agent captures the screen only while the CCC has granted a lease (docs/PROTOCOL.md "screen_lease");
        // every token and every renewal extends it by the same time as the viewer's own lease.
        if ($pc = Pc::find($audit->pc_id)) {
            app(CommandBus::class)->send($pc, 'screen_lease', ['ttl_s' => $exp - $now]);
        }

        $token = $this->encodeJwt([
            'iss' => $cfg['issuer'],
            'aud' => $cfg['audience'],
            'sub' => (string) $audit->instructor_id,
            'audit_id' => (int) $audit->id,
            'student_id' => (int) ($audit->student_id ?? 0),
            'lab_session_id' => (int) ($audit->lab_session_id ?? 0),
            'pc_id' => (int) $audit->pc_id,
            'stream_id' => (int) $audit->stream_id,
            'mode' => $audit->mode,
            'iat' => $now,
            'nbf' => $now,
            'exp' => $exp,
            'jti' => bin2hex(random_bytes(16)),
        ], $cfg);

        return [$token, $exp];
    }

    private function requireSecret(): void
    {
        abort_if((string) config('services.screen_gateway.secret', '') === '', Response::HTTP_INTERNAL_SERVER_ERROR,
            'Screen gateway secret is not configured');
    }

    private function encodeJwt(array $payload, array $cfg): string
    {
        $this->requireSecret();

        return JWT::encode($payload, (string) $cfg['secret'], 'HS256');
    }

    private function resolveGatewayUrl(Request $request, array $cfg): string
    {
        $configured = trim((string) ($cfg['url'] ?? ''));
        if ($configured !== '') {
            return rtrim($configured, '/');
        }
        $scheme = $request->isSecure() ? 'wss' : 'ws';
        $host = $request->getHost();
        $port = $request->getPort();
        $defaultPort = $request->isSecure() ? 443 : 80;
        $authority = $port && $port !== $defaultPort ? "{$host}:{$port}" : $host;
        $path = '/'.trim((string) ($cfg['path'] ?? '/screen-gw'), '/');

        return "{$scheme}://{$authority}".($path === '/' ? '' : $path);
    }
}
