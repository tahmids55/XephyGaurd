# F102. Shared clipboard / text share ⚠SEC ⚠PRIV

- **Component:** server
- **Area:** 16. Public pages
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> No login needed for any of these.

**Feature:**

- **What:** a page anyone can read and write; keeps the last 20 texts; polls every 2.5 s.
- **Trigger:** `GET /share` (+ `/share.html`), `POST /share`, `GET /share/raw`.
- **Code:** `PublicController::shareIndex`, `shareSave`, `shareRaw`; settings `shared_clipboard`,
  `shared_clipboard_at`, `shared_clipboard_history`.
- **Status:** working, and that is the problem: no login, works as a message board between exam
  seats (`docs/HISTORY.md`).
- **Test:** 1. Open `/share` logged out. 2. Post text. 3. `curl localhost:8000/share/raw` shows it.
