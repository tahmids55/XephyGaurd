# F4. CCC doctor (self-check)

- **Component:** server (also: ccc-deploy)
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** checks the CCC (DB, CA, Reverb, network state, services) and explains problems in plain
  language.
- **Trigger:** `php artisan xephyguard:doctor [--json]`; `xephyguard-ccc doctor` ([F113](../ccc-deploy/F113-xephyguard-ccc-admin-cli.md)); shown on the
  Health page ([F103](F103-health-page.md)).
- **Code:** `S/Console/Commands/DoctorCommand.php`, `S/Services/Doctor.php::run`.
- **In/out/side effects:** read-only checks; prints a list or JSON.
- **Status:** unclear (no test).
- **Test:** 1. `php artisan xephyguard:doctor`. 2. Stop Reverb and run it again; it should report it.
