<?php

namespace App\Http\Controllers\Api;

use App\Http\Controllers\Controller;
use App\Models\LabSession;
use App\Models\ScreenViewAudit;
use App\Models\SessionPc;
use App\Models\Student;
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
        if (!$user) {
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

        // Fallback: check if student belongs to an active lab session directly
        if (!$session) {
            $session = LabSession::active()
                ->where(function ($q) use ($student) {
                    $q->whereHas('sessionPcs', function ($sq) use ($student) {
                        $sq->where('student_id', $student->id);
                    });
                })
                ->first();
        }

        if (!$session) {
            return response()->json([
                'message' => 'No active lab session found for this student',
            ], Response::HTTP_NOT_FOUND);
        }

        // Verify the instructor owns the session (admins are allowed)
        if ($session->started_by !== $user->id && !$user->hasRole('admin')) {
            return response()->json([
                'message' => 'Forbidden: You do not own this lab session',
            ], Response::HTTP_FORBIDDEN);
        }

        $validated = $request->validate([
            'mode' => 'nullable|string|in:live,snapshot,wall',
            'stream_id' => 'nullable|integer',
        ]);

        $mode = $validated['mode'] ?? 'live';
        $streamId = (int) ($validated['stream_id'] ?? ($student->id * 1000 + $session->id));

        $cfg = config('services.screen_gateway');
        $now = time();
        $exp = $now + 30; // 30 second lease
        $jti = bin2hex(random_bytes(16));

        $payload = [
            'iss' => $cfg['issuer'],
            'aud' => $cfg['audience'],
            'sub' => (string) $user->id,
            'student_id' => (int) $student->id,
            'lab_session_id' => (int) $session->id,
            'stream_id' => $streamId,
            'mode' => $mode,
            'iat' => $now,
            'nbf' => $now,
            'exp' => $exp,
            'jti' => $jti,
        ];

        $token = JWT::encode($payload, $cfg['secret'], 'HS256');

        $audit = ScreenViewAudit::create([
            'instructor_id' => $user->id,
            'student_id' => $student->id,
            'lab_session_id' => $session->id,
            'stream_id' => $streamId,
            'mode' => $mode,
            'started_at' => now(),
        ]);

        return response()->json([
            'token' => $token,
            'stream_id' => $streamId,
            'audit_id' => $audit->id,
            'expires_at' => $exp,
            'gateway_url' => $cfg['url'],
            'mode' => $mode,
        ]);
    }

    /**
     * Renews the short-lived lease for an active stream view.
     */
    public function renewToken(Request $request, ScreenViewAudit $audit): JsonResponse
    {
        $user = $request->user();
        if (!$user) {
            abort(Response::HTTP_UNAUTHORIZED);
        }

        if ($audit->instructor_id !== $user->id && !$user->hasRole('admin')) {
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

        $cfg = config('services.screen_gateway');
        $now = time();
        $exp = $now + 30;
        $jti = bin2hex(random_bytes(16));

        $payload = [
            'iss' => $cfg['issuer'],
            'aud' => $cfg['audience'],
            'sub' => (string) $user->id,
            'student_id' => (int) $audit->student_id,
            'lab_session_id' => (int) ($audit->lab_session_id ?? 0),
            'stream_id' => (int) $audit->stream_id,
            'mode' => $audit->mode,
            'iat' => $now,
            'nbf' => $now,
            'exp' => $exp,
            'jti' => $jti,
        ];

        $token = JWT::encode($payload, $cfg['secret'], 'HS256');

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
        if (!$user) {
            abort(Response::HTTP_UNAUTHORIZED);
        }

        if ($audit->instructor_id !== $user->id && !$user->hasRole('admin')) {
            abort(Response::HTTP_FORBIDDEN);
        }

        if ($audit->ended_at === null) {
            $audit->update(['ended_at' => now()]);
        }

        return response()->json([
            'status' => 'stopped',
            'audit_id' => $audit->id,
            'duration_s' => $audit->ended_at->diffInSeconds($audit->started_at),
        ]);
    }
}
