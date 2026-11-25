# F8. Discovery service (UDP)

- **Component:** ccc-deploy (also: agent)
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** lets agents find the CCC on the LAN without a configured host.
- **Trigger:** UDP broadcast to port `XG_DISCOVERY_PORT` (47474); Docker service `discovery`.
- **Code:** `ccc-deploy/services/discovery.py`; agent side `A/core/src/discovery*` (reply parsing).
- **In/out/side effects:** replies with CCC address and CA fingerprint; no writes.
- **Status:** partly working (agent reply parsing tested; the service itself has no test).
- **Test:** 1. Start `python3 ccc-deploy/services/discovery.py`. 2. From a lab PC, run the agent
  enroll without `--ccc` and check it finds the server.
