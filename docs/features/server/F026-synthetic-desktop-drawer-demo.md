# F26. Synthetic desktop drawer (demo)

- **Component:** server
- **Area:** 4. Live lab dashboard
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** fake student desktops shown in the UI when no real stream exists.
- **Trigger:** Live lab page; gated off in `4ac2d8e`.
- **Code:** `server/resources/js/app.js`; `server/resources/demo/data.js` (demo data), `PublicController::demoData`.
- **Status:** unclear (leftover demo code; `docs/HISTORY.md` batch 6). The dataset is no longer in `public/`; `GET /js/data.js` serves it only when `APP_ENV=local` (404 elsewhere).
- **Test:** 1. `curl -si localhost:8000/js/data.js | head -1`: 404 unless `APP_ENV=local`.
