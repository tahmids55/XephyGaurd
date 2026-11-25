# F100. Linux install script

- **Component:** server (also: installers)
- **Area:** 16. Public pages
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> No login needed for any of these.

**Feature:**

- **What:** a shell script that installs the agent and enrolls it; used as `curl http://… | sudo bash`.
- **Trigger:** `GET /install/linux.sh`.
- **Code:** `PublicController::linuxInstaller`.
- **Status:** working (installer command test). ⚠SEC plain HTTP piped to root.
- **Test:** 1. `curl -s localhost:8000/install/linux.sh | head`.
