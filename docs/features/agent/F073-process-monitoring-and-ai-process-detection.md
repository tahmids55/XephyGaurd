# F73. Process monitoring and AI process detection

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** tracks process start/stop and snapshots; matches against process rules (AI apps, local
  LLM runners, CLI agents).
- **Events:** `process_start`, `process_stop`, `process_snapshot`, `rule_hit`.
- **Code:** `A/core/src/process_tracker.cpp`, `rules`; `A/platform/linux/process_monitor.cpp`,
  `A/platform/windows/process_monitor_win.cpp`.
- **Status:** working on Linux (`agent reports and warns on an AI process in Test mode`,
  `process tracker diffs snapshots`); Windows unclear.
- **Test:** 1. In a Test session run `ollama` (or a dummy named like a rule). 2. A rule hit alert appears.
