# F57. Pseudonymise roster

- **Component:** server
- **Area:** 10. Roster and consent
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `POST /roster/pseudonymize`. Admin only.
- **Code:** `RosterController::pseudonymize`; writes a setting; audit.
- **Status:** unclear (no test).
- **Test:** 1. Turn it on. 2. Reports show pseudonyms instead of names.
