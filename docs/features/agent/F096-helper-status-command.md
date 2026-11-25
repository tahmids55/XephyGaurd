# F96. Helper status command

- **Component:** agent
- **Area:** 15. Session helper (student desktop)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> `xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
> IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
> `notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

**Feature:**

- **Trigger:** `xephyguard-helper status`.
- **Code:** `A/apps/helper_main.cpp`.
- **Status:** unclear.
- **Test:** 1. Run it in the student session.
