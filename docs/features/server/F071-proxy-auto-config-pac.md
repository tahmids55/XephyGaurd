# F71. Proxy auto-config (PAC)

- **Component:** server (also: agent)
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** PAC file pointing PCs at the CCC proxy, used in proxy_only mode.
- **Trigger:** `GET /proxy.pac` (public); pushed to PCs as `proxy_pac_url`.
- **Code:** `PublicController::proxyPac`; agent/helper writes it to the OS ([F91](../agent/F091-browser-policies-and-dns-settings-on-the-pc.md)).
- **Status:** working (`proxy_pac_reflects_network_mode_and_policy_carries_pac_url`,
  `agent pushes proxy_pac_url to helper in proxy_only mode`). ⚠SEC plain HTTP, falls back to DIRECT.
- **Test:** 1. `curl localhost:8000/proxy.pac`.
