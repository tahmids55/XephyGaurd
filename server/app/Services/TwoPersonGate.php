<?php

namespace App\Services;

use App\Models\Setting;
use App\Models\Student;
use App\Models\TwoPersonRequest;
use App\Models\User;
use Illuminate\Database\Eloquent\Model;

/**
 * Enforces the Two-Person Rule (P3-C1) for high-sensitivity governance actions
 * when `Setting::get('two_person_rule', false)` is enabled.
 */
class TwoPersonGate
{
    /** @var array<int, callable> */
    private static array $callbacks = [];

    public static function enabled(): bool
    {
        return (bool) Setting::get('two_person_rule', config('xephyguard.two_person_rule', false));
    }

    /**
     * When `two_person_rule` is enabled, queues a `pending` TwoPersonRequest and returns it.
     * When disabled, executes `$execute` immediately and returns null.
     */
    public static function requestOrRun(
        User $by,
        string $action,
        ?Model $target,
        array $payload,
        string $reason,
        callable $execute
    ): ?TwoPersonRequest {
        if (! self::enabled()) {
            $execute();

            return null;
        }

        $req = TwoPersonRequest::create([
            'action' => $action,
            'target_type' => $target ? class_basename($target) : null,
            'target_id' => $target?->getKey(),
            'payload' => $payload,
            'reason' => $reason,
            'requested_by' => $by->id,
            'status' => 'pending',
        ]);

        self::$callbacks[$req->id] = $execute;

        Audit::log('two_person.requested', $req, [
            'action' => $action,
            'target_type' => $req->target_type,
            'target_id' => $req->target_id,
            'requested_by' => $by->id,
            'reason' => $reason,
        ]);

        return $req;
    }

    public static function approve(TwoPersonRequest $req, User $approver): TwoPersonRequest
    {
        if ($req->status !== 'pending') {
            abort(422, 'This two-person request has already been decided.');
        }
        if ((int) $req->requested_by === (int) $approver->id) {
            abort(422, 'Two-person rule requires a second distinct user to approve this request.');
        }

        self::executeAction($req, $approver);

        $req->update([
            'status' => 'approved',
            'approved_by' => $approver->id,
            'decided_at' => now(),
        ]);

        Audit::log('two_person.approved', $req, [
            'action' => $req->action,
            'target_type' => $req->target_type,
            'target_id' => $req->target_id,
            'requested_by' => $req->requested_by,
            'approved_by' => $approver->id,
        ]);

        return $req->fresh();
    }

    public static function reject(TwoPersonRequest $req, User $approver, ?string $reason = null): TwoPersonRequest
    {
        if ($req->status !== 'pending') {
            abort(422, 'This two-person request has already been decided.');
        }
        if ((int) $req->requested_by === (int) $approver->id) {
            abort(422, 'Two-person rule requires a second distinct user to decide this request.');
        }

        $req->update([
            'status' => 'rejected',
            'approved_by' => $approver->id,
            'decided_at' => now(),
        ]);

        unset(self::$callbacks[$req->id]);

        Audit::log('two_person.rejected', $req, [
            'action' => $req->action,
            'requested_by' => $req->requested_by,
            'rejected_by' => $approver->id,
            'reason' => $reason,
        ]);

        return $req->fresh();
    }

    private static function executeAction(TwoPersonRequest $req, User $approver): void
    {
        if (isset(self::$callbacks[$req->id])) {
            $cb = self::$callbacks[$req->id];
            unset(self::$callbacks[$req->id]);
            $cb();

            return;
        }

        $payload = is_array($req->payload) ? $req->payload : [];
        switch ($req->action) {
            case 'student.exempt':
                $student = Student::query()->findOrFail($req->target_id);
                $exempt = (bool) ($payload['exempt'] ?? true);
                $reason = $exempt ? (string) ($payload['reason'] ?? $req->reason ?? 'Approved exemption') : null;
                $student->update([
                    'exempt' => $exempt,
                    'exempt_reason' => $reason,
                ]);
                Audit::log('student.exempt', $student, [
                    'exempt' => $exempt,
                    'reason' => $reason,
                    'requested_by' => $req->requested_by,
                    'approved_by' => $approver->id,
                ]);
                break;

            case 'settings.pseudonymize':
            case 'roster.pseudonymize':
                $enabled = (bool) ($payload['enabled'] ?? false);
                Setting::put('pseudonymize_exports', $enabled);
                Audit::log('settings.pseudonymize', null, [
                    'enabled' => $enabled,
                    'requested_by' => $req->requested_by,
                    'approved_by' => $approver->id,
                ]);
                break;
        }
    }
}
