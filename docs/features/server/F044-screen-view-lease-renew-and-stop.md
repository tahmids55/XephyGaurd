# F44. Screen view lease renew and stop

- **Component:** server (also: screen)
- **Area:** 8. Screen viewing and evidence
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `POST /api/screen/{audit}/renew`, `POST /api/screen/{audit}/stop`.
- **Code:** `ViewTokenController::renewToken`, `stopView`; writes `screen_view_audits`.
- **Status:** working (`lease_renewal_and_stream_stop`).
- **Test:** 1. Keep a view open more than 30 s: it keeps playing. 2. Close it: the audit row gets an end time.
