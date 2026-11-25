# F108. Machine-silent detection — every 5 seconds

- **Component:** server
- **Area:** 18. Background jobs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> Run by `php artisan schedule:work` (Docker service `scheduler`) and `queue:work` (service `worker`).
> Defined in `server/routes/console.php`.

**Feature:**

- **What:** marks PCs silent after 3 missed heartbeats (`XG_HEARTBEAT_TIMEOUT`) and raises an alert,
  using [F25](F025-health-classification-of-pcs.md) for the label.
- **Code:** `SessionManager::detectSilent`.
- **Status:** working (`machine_silent_detection`).
- **Test:** 1. Stop an agent. 2. Within ~15 s the seat shows silent and an alert appears.
