# F54. Roster import

- **Component:** server
- **Area:** 10. Roster and consent
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** import students from CSV (`student_id`, `name`, `login`, with or without header).
- **Trigger:** `GET /roster` (+ `.html`), `POST /roster/import` (≤ 2 MB). Instructor and up.
- **Code:** `S/Http/Controllers/Web/RosterController.php::import`; writes `students`; audit.
- **Status:** unclear (no test).
- **Test:** 1. Upload a 3-row CSV. 2. `/roster` lists 3 students.
