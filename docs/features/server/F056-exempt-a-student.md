# F56. Exempt a student

- **Component:** server
- **Area:** 10. Roster and consent
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** marks a student as exempt (e.g. accommodations) so behaviour signals are ignored.
- **Trigger:** `POST /roster/{student}/exempt`.
- **Code:** `RosterController::exempt`; scoring honours it.
- **Status:** working (`exempt_student_behavior_signals_ignored`).
- **Test:** 1. Exempt a student. 2. Rescore: their behaviour reasons disappear.
