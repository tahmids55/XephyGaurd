# F76. Browser AI tab detection

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** flags AI sites seen in browser window/tab titles.
- **Code:** agent core (`web.ai_tab` rule hits), helper title feed.
- **Status:** partly working (see [F75](F075-window-focus-and-titles.md); hard-coded browser lists in `agent.cpp`).
- **Test:** 1. Open chatgpt.com in a Test session (if DNS is not blocking). 2. A tab alert appears.
