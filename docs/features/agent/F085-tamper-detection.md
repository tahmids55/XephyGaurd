# F85. Tamper detection

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** reports helper termination, binary hash mismatch (manifest), unclean restarts.
- **Events:** `tamper`, `helper_status`.
- **Code:** agent core (`integrity_check_s`, manifest), service host.
- **Status:** partly working (`unclean_restart_and_command_results`). False "terminated by user" on
  every helper disconnect, including logoff.
- **Test:** 1. Kill the helper process. 2. A tamper alert appears. 3. Log off: one appears too (false positive).
