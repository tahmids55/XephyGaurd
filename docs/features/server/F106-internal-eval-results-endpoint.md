# F106. Internal eval results endpoint

- **Component:** server
- **Area:** 17. Admin: health, settings, audit
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** red-team harness results for the evaluation page.
- **Trigger:** `GET /api/internal/eval/results` (internal token).
- **Code:** `S/Http/Controllers/Internal/EvalController.php`.
- **Status:** unclear (no test).
- **Test:** 1. `curl -H "Authorization: Bearer $XG_INTERNAL_TOKEN" localhost:8000/api/internal/eval/results`.
