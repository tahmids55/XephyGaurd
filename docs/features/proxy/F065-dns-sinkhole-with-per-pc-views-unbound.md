# F65. DNS sinkhole with per-PC views (Unbound)

- **Component:** proxy
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** PCs in a blocking session get NXDOMAIN/sinkhole for blocklisted domains; others resolve
  normally.
- **Trigger:** Docker service `netpolicy`; reads net state.
- **Code:** `proxy/xg_netpolicy/` (`generate.py`, `domains.py`, `state.py`), `proxy/dns/unbound.conf`,
  `ccc-deploy/docker/netpolicy-run.sh`.
- **Status:** working (`generate_outputs`, `state_groups`, `domain_suffix_matching`).
- **Test:** 1. Start a Test session. 2. On the PC `nslookup chatgpt.com <ccc-ip>` is blocked.
