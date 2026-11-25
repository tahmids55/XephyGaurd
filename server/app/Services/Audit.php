<?php

declare(strict_types=1);

namespace App\Services;

use App\Models\AuditLog;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Support\Facades\DB;

class Audit
{
    public static function log(string $action, ?Model $subject = null, array $details = []): AuditLog
    {
        $attrs = [
            'user_id' => auth()->id(),
            'action' => $action,
            'subject_type' => $subject ? class_basename($subject) : null,
            'subject_id' => $subject?->getKey(),
            'details' => $details ?: null,
            'ip' => request()?->ip(),
        ];

        // A failed audit write must fail the action: no unaudited changes (I-SERVER-14, DECISIONS #29).
        // The last row is locked until commit, so concurrent writers chain one after another (S-36).
        return DB::transaction(function () use ($attrs) {
            $last = DB::table('audit_log')->orderByDesc('id')->lockForUpdate()->first();
            $attrs['prev_hash'] = $last ? AuditChain::hash($last) : AuditChain::GENESIS;

            return AuditLog::create($attrs);
        }, 3);
    }
}
