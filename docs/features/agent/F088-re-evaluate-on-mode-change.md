# F88. Re-evaluate on mode change

- **Component:** agent
- **Area:** 14. Agent: enforcement on lab PCs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** apps already open when a Test starts are checked again under the new policy.
- **Code:** `Agent::reevaluate_all`.
- **Status:** working (`tools already running are re-evaluated when a Test session starts`).
- **Test:** 1. Open an AI app. 2. Start a Test session. 3. It is flagged/killed.
