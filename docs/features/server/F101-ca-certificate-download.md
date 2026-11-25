# F101. CA certificate download

- **Component:** server
- **Area:** 16. Public pages
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> No login needed for any of these.

**Feature:**

- **Trigger:** `GET /ca.crt`.
- **Code:** `PublicController::caCert`.
- **Status:** working.
- **Test:** see [F3](F003-internal-certificate-authority.md).
