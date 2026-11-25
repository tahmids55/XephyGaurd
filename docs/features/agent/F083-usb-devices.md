# F83. USB devices

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **Events:** `device`.
- **Code:** `A/platform/linux/device_monitor.cpp`, `A/platform/windows/device_win.cpp`, `devices.yaml`.
- **Status:** working on Linux (rule engine test); Windows unclear.
- **Test:** 1. Plug in a USB stick. 2. It shows on the USB tab.
