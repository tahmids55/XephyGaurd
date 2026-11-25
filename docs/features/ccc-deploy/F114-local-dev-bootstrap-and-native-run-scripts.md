# F114. Local dev bootstrap and native run scripts

- **Component:** ccc-deploy
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** setup scripts for a new teacher server or lab PC (Docker or native, Linux and Windows),
  and daily start/stop/status scripts.
- **Code:** `new_setup_scripts/` (`setup-server-docker.sh`, `setup-server-native.sh`,
  `install-agent.sh`, `setup-server.ps1/.bat`, `deploy-agents.ps1`), `start_server/` (`start-server`,
  `stop-server`, `status-server` for Linux and Windows), `ccc-deploy/bootstrap-local.sh`,
  `ccc-deploy/docker-compose.local.yml`, `ccc-deploy/import-sqlite-to-local.php`.
- **Status:** unclear (no tests; syntax-checked only for the `.sh` files).
- **Test:** 1. `start_server/linux/status-server.sh`. 2. It reports which services run.
