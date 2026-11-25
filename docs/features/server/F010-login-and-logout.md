# F10. Login and logout

- **Component:** server
- **Area:** 2. Staff accounts and access
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff sign in with email and password; logout ends the session.
- **Trigger:** `GET /login` (+ `/login.html`), `POST /login`, `POST /logout`.
- **Code:** `S/Http/Controllers/Web/AuthController.php` (`show`, `login`, `logout`).
- **In/out/side effects:** Laravel session; sets `users.last_login_at`; audit entry.
- **Status:** working (`first_run_wizard_then_login`).
- **Test:** 1. Open `/login`. 2. Log in. 3. Wrong password shows an error.
