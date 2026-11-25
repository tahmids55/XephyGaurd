# F75. Window focus and titles ⚠PRIV

- **Component:** agent
- **Area:** 13. Agent: monitoring on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
> and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
> `POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

**Feature:**

- **What:** reports the focused app and, depending on `title_capture` (off/app_only/full), the
  window title.
- **Events:** `window_focus`.
- **Code:** helper `A/platform/linux/x11_helper.cpp`, `A/platform/windows/helper_win.cpp`; filter in agent core.
- **Status:** working on X11 (`X11 window monitor survives 1000 rapid window open/close cycles`).
  Raw titles skip `filter_title` in `web.ai_tab` hits.
- **Test:** 1. Set title capture to full. 2. Focus a browser tab. 3. The inspector shows the title.
