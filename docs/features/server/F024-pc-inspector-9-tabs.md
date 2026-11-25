# F24. PC inspector (9 tabs)

- **Component:** server
- **Area:** 4. Live lab dashboard
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** per-PC detail: processes, extensions/installed AI, network/ports/adapters, USB devices,
  copy/paste, browser tabs, events timeline, screen, alerts.
- **Trigger:** `GET /pcs/{pc}` (JSON for the drawer). Assistant and up.
- **Code:** `DashboardController::pc` (also marks some data as seen).
- **Status:** working (`copy_from_chrome_and_paste_in_vscode_updates_dashboard`,
  `every_screen_renders_for_admin`). ⚠PRIV shows window/tab titles and file names ([F75](../agent/F075-window-focus-and-titles.md), [F79](../agent/F079-file-activity-in-work-directories.md)).
- **Test:** 1. Click a seat. 2. Open each tab and check it has data.
