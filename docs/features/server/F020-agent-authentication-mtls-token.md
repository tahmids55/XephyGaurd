# F20. Agent authentication (mTLS + token)

- **Component:** server (also: ccc-deploy)
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** every agent API call must carry a valid client cert (checked by nginx, passed as
  headers) and the agent's bearer token.
- **Trigger:** middleware `agent` on `/api/agent/v1/policy|events|broadcasting/auth`.
- **Code:** `S/Http/Middleware/AuthenticateAgent.php`; nginx `ccc-deploy/nginx/xephyguard.conf` (:8443).
- **Status:** working (`mtls_and_token_are_both_required`). Can be relaxed with `XG_REQUIRE_MTLS=false`.
- **Test:** 1. `curl -k https://<ccc>:8443/api/agent/v1/policy` without a cert: expect 4xx.
