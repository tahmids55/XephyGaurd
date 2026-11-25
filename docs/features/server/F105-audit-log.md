# F105. Audit log

- **Component:** server
- **Area:** 17. Admin: health, settings, audit
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** append-only record of staff actions (who, what, when, IP).
- **Trigger:** `GET /audit` (+ aliases). Admin only. Written by `S/Services/Audit.php::log` across the app.
- **Status:** partly working (`audit_log_is_append_only`). `Audit::log` swallows failures, so a
  failed insert leaves no record.
- **Test:** 1. Acknowledge an alert. 2. `/audit` shows the entry.
