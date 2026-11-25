# F18. Rename a PC

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `POST /pcs/{pc}/rename`. Assistant and up.
- **Code:** `EnrollmentController::rename`. Writes `pcs.name`; audit.
- **Status:** unclear (no test).
- **Test:** 1. Rename a PC from the inspector or `/enroll`. 2. The new name shows on the seat map.
