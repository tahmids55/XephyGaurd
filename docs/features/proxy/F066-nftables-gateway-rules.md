# F66. nftables gateway rules

- **Component:** proxy
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** forwards lab traffic, redirects HTTP(S) to the proxy, blocks DoH/DoT IPs and VPN ports.
- **Code:** `proxy/nftables/xephyguard.nft`, `xg_netpolicy` (`apply_skips_nft_in_proxy_only_or_without_table`).
- **Status:** partly working (ruleset syntax checked in CI; never run on a real gateway).
- **Test:** 1. On the CCC: `sudo nft list ruleset | grep xephyguard`.
