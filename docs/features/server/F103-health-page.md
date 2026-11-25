# F103. Health page

- **Component:** server
- **Area:** 17. Admin: health, settings, audit
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /health` (+ `/health.html`, `/admin-health.html`). Admin only.
- **Code:** `AdminController::health` (uses `Doctor`, agent stats).
- **Status:** working (renders in `every_screen_renders_for_admin`); checks themselves unclear ([F4](F004-ccc-doctor-self-check.md)).
- **Test:** 1. Open `/health`. 2. Each check has a green/red state.
