# F97. PAC and proxy settings for the user

- **Component:** agent
- **Area:** 15. Session helper (student desktop)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> `xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
> IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
> `notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

**Feature:**

- **What:** the helper applies or clears the PAC URL for the user session in proxy_only mode.
- **Status:** working (agent test for the push); OS side on Windows unclear.
- **Test:** 1. proxy_only mode. 2. Browser proxy settings on the PC show the PAC URL.
