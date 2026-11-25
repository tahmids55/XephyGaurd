<?php

declare(strict_types=1);

namespace App\Http\Controllers\Agent;

use App\Http\Controllers\Controller;
use App\Models\Enrollment;
use App\Models\EnrollmentToken;
use App\Models\Lab;
use App\Services\EnrollmentService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

class EnrollController extends Controller
{
    public function store(Request $request, EnrollmentService $svc): JsonResponse
    {
        $data = $request->validate([
            'enroll_key' => 'required_without:enroll_token|nullable|string|max:64',
            'enroll_token' => 'nullable|string|max:128',
            'hostname' => 'required|string|max:128',
            'os' => 'nullable|string|max:32',
            'os_version' => 'nullable|string|max:128',
            'macs' => 'nullable|array|max:16',
            'macs.*' => ['string', 'regex:/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i'],
            'machine_uid' => ['required', 'string', 'regex:/^[0-9a-f]{32}$/i'],   // DECISIONS #28
            'agent_version' => 'nullable|string|max:32',
            'csr_pem' => 'required|string|max:8192|starts_with:-----BEGIN CERTIFICATE REQUEST-----',
        ]);
        $lab = Lab::current();
        // A one-time per-PC token (staff-made on /enroll) auto-approves; the shared lab key alone only
        // creates a pending request (DECISIONS #28). A token that is given must be valid.
        $enrollToken = null;
        if (($data['enroll_token'] ?? null) !== null) {
            $enrollToken = EnrollmentToken::findUsable($lab, (string) $data['enroll_token']);
            if (! $enrollToken) {
                return response()->json(['error' => 'invalid, used or expired enrollment token'], 403);
            }
        } elseif (! $lab->checkEnrollKey((string) $data['enroll_key'])) {
            return response()->json(['error' => 'invalid enrollment key'], 403);
        }
        if (@openssl_csr_get_public_key($data['csr_pem']) === false) {
            return response()->json(['error' => 'invalid CSR'], 422);
        }
        ['enrollment' => $e, 'poll_token' => $token] = $svc->register($lab, $data, $request->ip(), $enrollToken);

        return response()->json(['enrollment_id' => $e->id, 'poll_token' => $token, 'status' => $e->status], 201);
    }

    public function show(Request $request, Enrollment $enrollment, EnrollmentService $svc): JsonResponse
    {
        $res = $svc->poll($enrollment, (string) $request->query('poll_token'));

        return $res === null ? response()->json(['error' => 'forbidden'], 403) : response()->json($res);
    }
}
