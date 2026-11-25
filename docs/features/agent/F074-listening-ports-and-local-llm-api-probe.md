# F74. Listening ports and local LLM API probe

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** finds listening ports; common dev ports only count if a probe gets an LLM-API-like reply.
- **Events:** `listening_port`, `rule_hit`.
- **Code:** `A/platform/linux/network_monitor.cpp`, `A/platform/windows/network_win.cpp`, agent `scan_ports`.
- **Status:** working (`common dev ports need a positive LLM-API probe`).
- **Test:** 1. Run `python -m http.server 11434`. 2. No alert. 3. A fake `/v1/models` server on 11434 raises one.
