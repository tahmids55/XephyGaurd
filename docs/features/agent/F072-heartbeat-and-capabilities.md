# F72. Heartbeat and capabilities

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** every `heartbeat_s` (5 s) the agent reports it is alive, its signature version and which
  collectors actually work on this PC.
- **Events:** `heartbeat`, `capabilities`, `agent_started`, `agent_stopping`.
- **Status:** working (`agent heartbeat reports honest capabilities.gpu…`, `machine_silent_detection`).
- **Test:** 1. `sudo xephyguard-agent status`. 2. The seat shows "last seen" a few seconds ago.
