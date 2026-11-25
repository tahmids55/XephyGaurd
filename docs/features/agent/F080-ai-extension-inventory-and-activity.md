# F80. AI extension inventory and activity

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** lists installed IDE/browser extensions, flags AI ones, and alerts when they are
  actively used (or in Test mode).
- **Events:** `extension_scan`, `rule_hit`.
- **Code:** `A/platform/linux/extension_scanner.cpp`, `A/platform/windows/extensions_win.cpp`, agent core.
- **Status:** working (`agent inventories installed AI extensions and alerts on test mode or active use`,
  `…matches copilot-proxy`). Activity heuristic is weak; IDE lists hard-coded.
- **Test:** 1. Install GitHub Copilot in VS Code. 2. It shows on the Installed tab.
