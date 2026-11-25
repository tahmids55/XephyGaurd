# F30. Built-in profiles

- **Component:** server
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** Always-on (no session), Practice: AI allowed + logged, Practice: AI blocked,
  Test: AI blocked, Exam: strict.
- **Code:** `S/Services/Bootstrap.php:15-27`.
- **Status:** working. ⚠PRIV window titles, clipboard and keystroke timing are on in every
  profile, including Always-on, and `4ac2d8e` turns on `files`/`artifacts` at boot (`docs/HISTORY.md`).
- **Test:** 1. `/profiles`: 5 profiles. 2. Open Always-on and check which collectors are on.
