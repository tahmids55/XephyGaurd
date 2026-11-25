# F23. Live lab seat map

- **Component:** server
- **Area:** 4. Live lab dashboard
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** grid of seats with online/offline status, colour by open alerts and live score,
  updated live over Reverb.
- **Trigger:** `GET /` (+ `/index.html`). Assistant and up. Events `PcUpdated`, `AlertRaised`,
  `SessionChanged` on `lab.{id}`.
- **Code:** `S/Http/Controllers/Web/DashboardController.php::index`, `resources/views/dashboard/live.blade.php`,
  `resources/js/app.js`, `S/Events/*`.
- **Status:** working (`dashboard_includes_all_52_unacknowledged_alerts_including_retired_pcs`,
  `dashboard_reconciles_stale_live_score_when_no_open_alerts_exist`).
- **Test:** 1. Open `/` with one PC online. 2. Stop the agent: the seat goes offline within ~15 s.
