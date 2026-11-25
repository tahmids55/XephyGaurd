# F90. Exam lockdown on Windows

- **Component:** agent
- **Area:** 14. Agent: enforcement on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** disables Task Manager (`DisableTaskMgr`) in Test/Exam mode.
- **Code:** `A/platform/windows/system_win.cpp`.
- **Status:** unclear (Windows only). Applied to every Test profile and stays set if the agent crashes.
- **Test:** only on Windows: start a Test session, press Ctrl+Shift+Esc.
