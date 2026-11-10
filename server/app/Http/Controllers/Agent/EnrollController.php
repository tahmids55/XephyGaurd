<?php

namespace App\Http\Controllers\Agent;

use App\Http\Controllers\Controller;
use App\Models\Enrollment;
use App\Models\Lab;
use App\Services\EnrollmentService;
use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;

class EnrollController extends Controller
{
    public function store(Request $request, EnrollmentService $svc): JsonResponse
    {
        $data = $request->validate([
            'enroll_key' => 'required|string|max:64',
            'hostname' => 'required|string|max:128',
            'os' => 'nullable|string|max:32',
            'os_version' => 'nullable|string|max:128',
            'macs' => 'nullable|array|max:16',
            'macs.*' => ['string', 'regex:/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i'],
            'machine_uid' => ['nullable', 'string', 'regex:/^[0-9a-f]{32}$/i'],
            'agent_version' => 'nullable|string|max:32',
            'csr_pem' => 'required|string|max:8192|starts_with:-----BEGIN CERTIFICATE REQUEST-----',
        ]);
        $lab = Lab::current();
        if (! $lab->checkEnrollKey($data['enroll_key'])) {
            return response()->json(['error' => 'invalid enrollment key'], 403);
        }
        if (@openssl_csr_get_public_key($data['csr_pem']) === false) {
            return response()->json(['error' => 'invalid CSR'], 422);
        }
        ['enrollment' => $e, 'poll_token' => $token] = $svc->register($lab, $data, $request->ip());

        return response()->json(['enrollment_id' => $e->id, 'poll_token' => $token, 'status' => $e->status], 201);
    }

    public function show(Request $request, Enrollment $enrollment, EnrollmentService $svc): JsonResponse
    {
        $res = $svc->poll($enrollment, (string) $request->query('poll_token'));

        return $res === null ? response()->json(['error' => 'forbidden'], 403) : response()->json($res);
    }
}
