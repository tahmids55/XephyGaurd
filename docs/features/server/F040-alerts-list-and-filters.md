# F40. Alerts list and filters

- **Component:** server
- **Area:** 7. Alerts and review
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /alerts` (+ `.html`). Assistant and up.
- **Code:** `S/Http/Controllers/Web/AlertController.php::index`.
- **Status:** working (`alerts_open_filter_and_json_acknowledge_handle_retired_pcs`).
- **Test:** 1. Open `/alerts`. 2. Filter "open": only unacknowledged alerts.
