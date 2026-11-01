<?php

namespace App\Jobs;

use App\Models\Alert;
use App\Models\LabSession;
use App\Models\NetEvent;
use App\Models\Report;
use App\Services\AnalysisClient;
use Illuminate\Contracts\Queue\ShouldQueue;
use Illuminate\Foundation\Queue\Queueable;

/** After a session ends: compute integrity scores (analysis service) and create the session report. */
class ScoreSession implements ShouldQueue
{
    use Queueable;

    public int $tries = 3;
    public array $backoff = [30, 120, 600];

    public function __construct(public int $sessionId) {}

    public function handle(AnalysisClient $analysis): void
    {
        $session = LabSession::query()->findOrFail($this->sessionId);
        $report = Report::query()->firstOrCreate(['lab_session_id' => $session->id, 'kind' => 'session'], ['status' => 'queued']);
        $result = $analysis->scoreSession($session->id);
        $report->forceFill([
            'status' => 'ready',
            'summary' => [
                'scored' => $result['scored'] ?? 0,
                'flagged' => $result['flagged'] ?? [],
                'alerts' => Alert::query()->where('lab_session_id', $session->id)->count(),
                'blocked_attempts' => NetEvent::query()->where('lab_session_id', $session->id)->where('action', 'blocked')->count(),
                'generated_at' => now()->toIso8601String(),
            ],
        ])->save();
    }

    public function failed(\Throwable $e): void
    {
        Report::query()->updateOrCreate(['lab_session_id' => $this->sessionId, 'kind' => 'session'],
            ['status' => 'failed', 'summary' => ['error' => mb_substr($e->getMessage(), 0, 500)]]);
    }
}
