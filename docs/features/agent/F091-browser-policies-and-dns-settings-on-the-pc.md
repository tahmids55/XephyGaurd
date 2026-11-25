# F91. Browser policies and DNS settings on the PC

- **Component:** agent (also: installers)
- **Area:** 14. Agent: enforcement on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** writes Chromium/Edge/Firefox policies (CA trust, PAC, AI Overview suppression) and a
  systemd-resolved drop-in pointing DNS at the CCC; Windows changes HKLM browser/DNS policies.
- **Code:** Linux helper/platform (`Linux browser policy writer…`, `Linux systemd-resolved drop-in…`),
  `installers/linux/install-browser-ca.sh`, `installers/browser-policies/*.json`, Windows `system_win.cpp`.
- **Status:** working on Linux (2 agent tests); Windows unclear.
- **Test:** 1. On Linux, check `/etc/opt/chrome/policies/managed/`. 2. `resolvectl status` shows the CCC.
