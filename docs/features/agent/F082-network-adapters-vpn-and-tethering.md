# F82. Network adapters, VPN and tethering

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** detects new adapters, VPNs, phone tethering and a second default route.
- **Events:** `net_adapter`, `rule_hit`.
- **Code:** network monitors (Linux/Windows), `data/signatures/adapters.yaml`.
- **Status:** working (`agent detects tethering, VPN and a second default route`).
- **Test:** 1. Plug in a USB tether. 2. An adapter alert appears.
