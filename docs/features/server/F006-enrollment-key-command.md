# F6. Enrollment key command

- **Component:** server
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** shows or rotates the lab enrollment key from the shell.
- **Trigger:** `php artisan xephyguard:enroll-key [--rotate]`; `xephyguard-ccc enroll-key`.
- **Code:** `S/Console/Commands/EnrollKey.php`, `S/Models/Lab.php::enrollKey`.
- **In/out/side effects:** `--rotate` updates the lab's key (old key stops working).
- **Status:** working (`key_rotation_invalidates_old_key` covers rotation).
- **Test:** 1. Run it twice with `--rotate`. 2. An agent enrolling with the old key is rejected.
