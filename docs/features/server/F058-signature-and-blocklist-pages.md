# F58. Signature and blocklist pages

- **Component:** server
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /signatures` (+ `.html`). Admin only.
- **Code:** `S/Http/Controllers/Web/SignatureController.php::index`.
- **Status:** working (renders for admin).
- **Test:** 1. Open `/signatures`. 2. Blocklist groups, rules and versions show.
