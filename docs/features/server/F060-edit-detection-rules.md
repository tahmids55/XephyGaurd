# F60. Edit detection rules

- **Component:** server
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** save or toggle process/extension/port/artifact/adapter/device rules.
- **Trigger:** `POST /rules`, `POST /rules/{rule}/toggle`.
- **Code:** `SignatureController::saveRule`, `toggleRule`; writes `rules`; audit.
- **Status:** unclear (no test for the endpoints).
- **Test:** 1. Disable a rule. 2. After publish ([F61](F061-publish-and-roll-back-signature-bundles.md)/F110) the agent stops flagging it.
