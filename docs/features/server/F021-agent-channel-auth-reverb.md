# F21. Agent channel auth (Reverb)

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** an agent may subscribe only to its own `private-pc.<id>` command channel.
- **Trigger:** `POST /api/agent/v1/broadcasting/auth`.
- **Code:** `S/Http/Controllers/Agent/AgentController.php::broadcastAuth`.
- **Status:** working (`broadcast_auth_only_for_own_channel`).
- **Test:** covered by the test; by hand, request another PC's channel and expect 403.
