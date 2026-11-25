# F84. GPU usage sampling

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** samples GPU busy % (a local LLM sign).
- **Events:** `gpu_sample`.
- **Code:** `A/platform/linux/gpu_monitor.cpp`.
- **Status:** working on Intel/AMD Linux (`Linux GpuMonitor reads Intel i915/Xe RC6 residency…`).
- **Test:** 1. Turn on `gpu` in a profile. 2. Run a GPU load. 3. `gpu_sample` events rise.
