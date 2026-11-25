# F11. Role-based access

- **Component:** server
- **Area:** 2. Staff accounts and access
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** pages and actions are limited by role; admins can do everything.
- **Trigger:** every route in the `role:` groups; nav items hidden by role.
- **Code:** `S/Http/Middleware/RequireRole.php`, `User::hasRole`, `routes/web.php`.
- **Status:** working (`roles_are_enforced_server_side`).
- **Test:** 1. Log in as an assistant. 2. Open `/settings`: expect 403.
