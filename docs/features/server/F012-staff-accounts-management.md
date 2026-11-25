# F12. Staff accounts management

- **Component:** server
- **Area:** 2. Staff accounts and access
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** admin lists, creates and edits staff accounts (name, email, role, active).
- **Trigger:** `GET /users` (+ `.html` aliases), `POST /users/{user?}`. Admin only.
- **Code:** `S/Http/Controllers/Web/AdminController.php` (`users`, `saveUser`).
- **In/out/side effects:** writes `users`; audit entry.
- **Status:** partly working (renders in `every_screen_renders_for_admin`; create/edit not tested).
- **Test:** 1. `/users` → add an assistant. 2. Log in as that user.
