# F28. Per-PC profile override

- **Component:** server
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** instructor gives one PC a different profile inside a session, with a reason.
- **Trigger:** `POST /sessions/{session}/pcs/{pc}/override`. Instructor and up.
- **Code:** `SessionController::override`, `SessionManager::setOverride`.
- **Status:** unclear (no test).
- **Test:** 1. In a Test session, override one PC to Practice. 2. AI works on that PC only.
