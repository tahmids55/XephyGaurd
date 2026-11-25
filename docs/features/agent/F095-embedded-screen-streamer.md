# F95. Embedded screen streamer ⚠PRIV

- **Component:** agent (also: screen)
- **Area:** 15. Session helper (student desktop)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> `xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
> IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
> `notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

**Feature:**

- **What:** captures the desktop and streams to the gateway when a viewer asks.
- **Code:** `A/apps/screen_streamer.cpp`, `xephyguard-screen/capture/x11_capture.cpp`; config
  `[screen]` / `XG_SCREEN_*`.
- **Status:** partly working (see [F43](../screen/F043-live-screen-view-of-a-pc.md)). ⚠SEC X11 shared-memory buffer mode `0777`.
- **Test:** see [F43](../screen/F043-live-screen-view-of-a-pc.md).
