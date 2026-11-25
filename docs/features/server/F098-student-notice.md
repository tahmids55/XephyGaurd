# F98. Student notice

- **Component:** server
- **Area:** 16. Public pages
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> No login needed for any of these.

**Feature:**

- **Trigger:** `GET /notice` (+ `.html`).
- **Code:** `PublicController::notice`, `resources/views/public/notice.blade.php`.
- **Status:** working (see [F33](F033-student-notice-in-policy.md)); the screen row and "Screen recordings" item follow the enabled features.
- **Test:** 1. Open `/notice` logged out.
