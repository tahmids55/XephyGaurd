# F79. File activity in work directories ⚠PRIV

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** notes file creates/changes in configured `work_dirs` (names/sizes, not content).
- **Events:** `file_activity`.
- **Code:** `A/platform/linux/file_watcher.cpp`, `A/platform/windows/file_watcher_win.cpp`.
- **Status:** unclear (no test; turned on at boot by `4ac2d8e`).
- **Test:** 1. Create a file in a work dir. 2. A `file_activity` event shows in the timeline.
