# F25. Health classification of PCs

- **Component:** server
- **Area:** 4. Live lab dashboard
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** labels a silent PC as `AGENT_UNAVAILABLE` or `SUSPECTED_TAMPERING` by comparing with its
  neighbours.
- **Trigger:** used by the dashboard and machine-silent detection ([F108](F108-machine-silent-detection-every-5-seconds.md)).
- **Code:** `S/Services/AgentHealthClassifier.php::classify`.
- **Status:** partly working (10 unit tests pass; returns a label without traceable reasons).
- **Test:** 1. Stop the agent on one PC while others run. 2. After 3 missed heartbeats it is marked suspected tampering.
