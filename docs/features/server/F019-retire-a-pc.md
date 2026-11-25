# F19. Retire a PC

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** removes a PC from the lab; its agent is refused afterwards.
- **Trigger:** `POST /pcs/{pc}/retire`. Admin only.
- **Code:** `EnrollmentController::retire`. Sets `pcs.retired_at`, revokes agents, rewrites net state; audit.
- **Status:** working (`revoked_and_retired_agents_are_refused`).
- **Test:** 1. Retire a PC. 2. Its agent's next request gets 403.
