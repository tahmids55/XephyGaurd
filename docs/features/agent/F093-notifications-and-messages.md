# F93. Notifications and messages

- **Component:** agent
- **Area:** 15. Session helper (student desktop)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> `xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
> IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
> `notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

**Feature:**

- **What:** shows staff messages and warnings (desktop notification; on Windows also a modal box).
- **Status:** partly working (Linux tested through the command test; Windows unclear, modal boxes stack).
- **Test:** see [F34](../server/F034-send-a-message-to-students.md).
