# F104. Settings

- **Component:** server
- **Area:** 17. Admin: health, settings, audit
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** lab name, network mode, CCC IP, retention (raw 7–1095 days, reports 30–3650 days),
  notice text, paste alert size, timezone, title capture per mode, `log_urls`, `log_queries`,
  inspect hosts, Google hide selectors, search template URL.
- **Trigger:** `GET /settings` (+ aliases), `POST /settings`. Admin only.
- **Code:** `AdminController::settings`, `saveSettings`; writes `settings`; rewrites net state; audit.
- **Status:** working (`save_settings_accepts_search_template_url_and_gates_log_queries`).
- **Test:** 1. Change the lab name. 2. The header shows the new name.
