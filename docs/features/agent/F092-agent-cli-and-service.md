# F92. Agent CLI and service

- **Component:** agent (also: installers)
- **Area:** 14. Agent: enforcement on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** `xephyguard-agent enroll|run [--foreground]|status|version`, `--config`; systemd unit
  (watchdog) or Windows service.
- **Code:** `A/apps/agent_main.cpp`, `A/platform/linux/service_host.cpp`,
  `installers/linux/xephyguard-agent.service`.
- **Status:** working on Linux (built and tests passed today); Windows service unclear.
- **Test:** 1. `xephyguard-agent version`. 2. `systemctl status xephyguard-agent`.
