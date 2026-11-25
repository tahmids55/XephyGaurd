# F13. Dashboard live channel auth

- **Component:** server
- **Area:** 2. Staff accounts and access
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** dashboard users subscribe to the lab's live channel; nobody but the agent may join a
  PC channel.
- **Trigger:** Reverb private channel `lab.{labId}`; `pc.{pcId}` always denied for users.
- **Code:** `server/routes/channels.php`.
- **Status:** working (`broadcast_auth_only_for_own_channel` covers the agent side).
- **Test:** 1. Open the dashboard with dev tools. 2. The WebSocket subscribes to `private-lab.1`.
