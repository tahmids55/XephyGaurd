# F99. Downloads and install commands ⚠SEC

- **Component:** server
- **Area:** 16. Public pages
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> No login needed for any of these.

**Feature:**

- **What:** lists installers in `storage/app/public/downloads` with sizes and ready-to-copy install
  commands that include the **real enrollment key** and the CA fingerprint.
- **Trigger:** `GET /downloads` (+ `.html`), `GET /downloads/{file}` (`.deb .rpm .msi .exe .sha256 .bat`).
- **Code:** `PublicController::downloadsIndex`, `download`; `resources/views/public/downloads.blade.php`.
- **Status:** working (`downloads_layout_handles_guest_no_rail_and_kb_sizes`,
  `downloads_serves_deb_and_generates_http_linux_installer_command`, `download_allows_bat_scripts`).
  The key is shown to anyone.
- **Test:** 1. Open `/downloads` logged out. 2. The enrollment key is visible.
