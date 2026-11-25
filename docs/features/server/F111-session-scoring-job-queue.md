# F111. Session scoring job (queue)

- **Component:** server
- **Area:** 18. Background jobs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> Run by `php artisan schedule:work` (Docker service `scheduler`) and `queue:work` (service `worker`).
> Defined in `server/routes/console.php`.

**Feature:**

- `S/Jobs/ScoreSession.php`, see [F48](../analysis/F048-session-scoring-v1-rules-v2-isolation-forest.md). Status: working.
