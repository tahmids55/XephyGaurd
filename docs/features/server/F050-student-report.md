# F50. Student report

- **Component:** server
- **Area:** 9. Scoring, reports and research
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /reports/{session}/students/{student}`.
- **Code:** `ReportController::student`.
- **Status:** unclear (no dedicated test).
- **Test:** 1. Click a student in a report. 2. The timeline and reasons show.
