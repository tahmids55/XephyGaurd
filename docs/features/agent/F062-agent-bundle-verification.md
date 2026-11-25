# F62. Agent bundle verification

- **Component:** agent
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** the agent checks the bundle signature against the lab CA and refuses one without
  domains, keeping the last good bundle.
- **Code:** `A/core/src/` (signature verification, `crypto`), `rules`.
- **Status:** working (`signature bundle verification with a CA certificate`,
  `agent refuses a bundle without domains and keeps the last-known-good one`).
- **Test:** covered by tests.
