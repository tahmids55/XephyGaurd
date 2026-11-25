# F64. Gateway or proxy-only network mode

- **Component:** proxy (also: server, ccc-deploy)
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** the CCC is either the lab's router (gateway: DNS + nftables + transparent proxy) or only a
  proxy reached by PAC (proxy_only).
- **Trigger:** setting `network_mode` ([F104](../server/F104-settings.md)), `XG_NETWORK_MODE`; installer question.
- **Code:** `NetworkPolicyWriter`, `proxy/xg_netpolicy/generate.py`, `ccc-deploy/docker/proxy-entrypoint.sh`.
- **Status:** partly working (tests pass; gateway mode never run on a real two-NIC host).
- **Test:** 1. Set proxy_only. 2. `curl localhost:8000/proxy.pac` returns a PAC pointing at the CCC.
