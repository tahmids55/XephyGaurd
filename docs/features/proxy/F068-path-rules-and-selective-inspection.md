# F68. Path rules and selective inspection

- **Component:** proxy
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** for chosen hosts (`inspect_hosts`) the proxy decrypts and blocks specific paths.
- **Code:** addon `request`, `proxy_logic.py`.
- **Status:** working (`path_rule_blocked`, `plain_http_blocked_domain`).
- **Test:** 1. Add a host/path rule. 2. That path is blocked, the rest of the site works.
