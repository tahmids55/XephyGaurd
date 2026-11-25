# F1. First-run setup wizard

- **Component:** server
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** a web form that creates the first admin account and the lab (name, CCC IP, network
  mode) on a fresh server.
- **Trigger:** `GET /setup`, `POST /setup` (throttled 10/min). Available until an admin exists. Submitting needs the `XG_SETUP_TOKEN` printed by install.sh, unless opened directly on the CCC via loopback (S-9).
- **Code:** `S/Http/Controllers/Web/SetupController.php` (`show`, `store`), `S/Services/Bootstrap.php`.
- **In/out/side effects:** writes `users`, `labs`, `settings`; regenerates network state
  (`NetworkPolicyWriter::write` → `storage/app/net/state.json`); audit entry.
- **Status:** working (`first_run_wizard_then_login`).
- **Test:** 1. Fresh DB, open `/setup`. 2. Fill the form. 3. You are sent to `/login` and can log in.
