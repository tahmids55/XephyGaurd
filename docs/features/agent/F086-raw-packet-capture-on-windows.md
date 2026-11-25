# F86. Raw packet capture on Windows ⚠PRIV

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** `SIO_RCVALL` raw socket as SYSTEM for traffic observation.
- **Code:** `A/platform/windows/network_win.cpp:535`.
- **Status:** unclear (Windows, compile-checked only).
- **Test:** only on a real Windows PC.
