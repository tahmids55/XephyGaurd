# F94. Monitoring status indicator

- **Component:** agent
- **Area:** 15. Session helper (student desktop)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> `xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
> IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
> `notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

**Feature:**

- **What:** an on-screen indicator that shows the current mode/monitoring, placed clear of the top bar.
- **Code:** `A/platform/linux/x11_helper.cpp` (`X11StudentUi`).
- **Status:** working on X11 (`Linux indicator geometry avoids top bar…`, `X11StudentUi maps indicator…`).
  Wayland not supported.
- **Test:** 1. Log in on an X11 lab PC. 2. The indicator shows the current mode.
