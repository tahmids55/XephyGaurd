# F81. Local AI model files

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** scans home dirs and removable media for `*.gguf`, `*.safetensors`, Ollama/LM Studio stores.
- **Events:** `artifact_scan` (shown on the Installed tab, not as alerts outside Test mode).
- **Code:** `A/core/src/artifact_scan.cpp`.
- **Status:** working (`artifact scan gives each root its own file budget…`).
- **Test:** 1. `truncate -s 10M ~/model.gguf`. 2. Rescan artifacts. 3. It shows on the Installed tab.
