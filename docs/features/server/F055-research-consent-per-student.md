# F55. Research consent per student

- **Component:** server
- **Area:** 10. Roster and consent
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `POST /roster/{student}/consent`.
- **Code:** `RosterController::consent`; writes `consents`; audit.
- **Status:** working through [F52](F052-research-export.md)'s test; the button itself is not tested.
- **Test:** 1. Toggle consent. 2. Check the research export ([F52](F052-research-export.md)).
