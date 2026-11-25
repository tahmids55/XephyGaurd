# F32. Policy delivery to agents

- **Component:** server (also: agent)
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** each agent fetches its effective policy (profile + session + overrides + signatures
  version + PAC URL + screen config).
- **Trigger:** `GET /api/agent/v1/policy` (agent); also pushed by `policy_updated`.
- **Code:** `AgentController::policy`, `S/Services/PolicyBuilder.php` (`forPc`, `effective`, `response`).
- **Status:** working (`policy_includes_browser_ai_overview_suppression`,
  `policy_builder_screen_config_handles_direct_port_and_tls_reverse_proxy`).
- **Test:** 1. Change the session profile. 2. The agent logs "policy applied" within seconds.
