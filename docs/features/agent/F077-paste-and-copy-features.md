# F77. Paste and copy features

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** for each paste: size, source app, target app, timing; copy events with app names. No
  clipboard text leaves the function (`paste.cpp`).
- **Events:** `paste`, `copy`; alert when over `paste_alert_chars`.
- **Code:** `A/core/src/paste.cpp`, helper `paste_probe`; `EventIngestor`.
- **Status:** working (`clipboard text is reduced to features`,
  `copy_from_chrome_and_paste_in_vscode_updates_dashboard`). ⚠PRIV file names leak into
  copy/paste events (`docs/HISTORY.md` batch 2).
- **Test:** 1. Copy 2,000 chars from a browser into an editor. 2. A paste event with size and apps shows.
