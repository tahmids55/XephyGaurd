# F22. Seat layout editor

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff arrange PCs on the seat map and save the layout.
- **Trigger:** `POST /lab/layout` from the Live lab page. Assistant and up.
- **Code:** `DashboardController::saveLayout`; writes/deletes `seats`; audit.
- **Status:** unclear (no test).
- **Test:** 1. Drag a PC to another seat and save. 2. Reload: it stays there.
