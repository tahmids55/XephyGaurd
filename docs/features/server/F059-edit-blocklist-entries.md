# F59. Edit blocklist entries

- **Component:** server
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** add, import, toggle or delete domains (and host/path rules) in groups (AI chat, IDE
  backends, marketplaces, DoH/DoT, VPN/proxy).
- **Trigger:** `POST /blocklist`, `POST /blocklist/import`, `POST /blocklist/{entry}/toggle`,
  `DELETE /blocklist/{entry}`.
- **Code:** `SignatureController`, `S/Services/BlocklistService.php`; writes `blocklist_entries`;
  rewrites net state; audit.
- **Status:** working (`blocklist_edit_hot_reloads_network_state`).
- **Test:** 1. Add `example.org` to AI chat. 2. In a Test session the PC can't resolve it.
