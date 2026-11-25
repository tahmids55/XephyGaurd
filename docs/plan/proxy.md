# Plan: proxy/ (mitmproxy addon, xg_netpolicy, Unbound, nftables)

Sources: [audit/proxy.md](../audit/proxy.md) (I-PROXY-1..32), [features/proxy/](../features/proxy/README.md) (F64-F70).
Plan written 2026-11-17. Nothing run while planning.

Rules for every step: one component, <300 changed lines, one kind of change, test in the same step, then
`python3 -m pytest -q proxy/tests` and `ruff check proxy --select E,F,W --ignore E501,E731`.
**[APPROVAL]** = changes a documented feature; **[CROSS]** = another component's plan has the matching step.
Steps that touch `xephyguard.nft` or `unbound.conf` are "config-checked only" unless run with `nft -c -f` / `unbound-checkconf` (run them where available and say so).

## Phase 1: Critical/High security and bugs
- [ ] Step 1: `server_connect` refuses loopback, link-local, CCC-own addresses and non-80/443 ports for every client (fixes I-PROXY-1; touches F64, F67, F68, F70)
- [ ] Step 2: Unbound remote control on a unix socket (0660) or cert-protected; keys only in the netpolicy container (fixes I-PROXY-2; touches F65) [CROSS: ccc-deploy compose volumes]
- [ ] Step 3: `input` chain: drop proxy/DNS/other ports arriving on `@WAN_IF@`, accept established and required ports only (fixes I-PROXY-3; touches F66) [CROSS: ccc-deploy I-CCC-4 covers `proxy_only` mode]
- [ ] Step 4: Put isolated/test drops before `ct state established,related accept`; flush conntrack (`conntrack -D -s <ip>`) for IPs added to test/isolated sets (fixes I-PROXY-4; touches F66, F65)
- [ ] Step 5: Re-apply `sets.nft` on startup and on nftables reload even if file content is unchanged (fixes I-PROXY-5; touches F66)
- [ ] Step 6: Watch loop catches all expected exception types per iteration and keeps running; logs and counts failures (fixes I-PROXY-6; touches F64, F65, F66)
- [ ] Step 7: One out-of-subnet or non-IPv4 group IP is skipped with a warning instead of rejecting the whole document (fixes I-PROXY-7; touches F64, F65, F66)
- [ ] Step 8: `inspect_hosts` matches exact hosts or listed subdomains, not whole `google.com`/`bing.com`; accounts, mail, docs excluded (fixes I-PROXY-8; touches F68, F69) [APPROVAL: narrows F69 coverage; update ETHICS.md]
- [ ] Step 9: With `log_urls.query`, strip the query string before shipping; default to host + path only (fixes I-PROXY-9; touches F68, F70) [update ETHICS.md if wording changes]

## Phase 2: Tooling
- [ ] Step 10: Ruff baseline clean for `proxy/` with the CI flags (fix findings only, no behavior change) (fixes none; touches F64-F70)
- [ ] Step 11: Add tests that run `nft -c -f` and `unbound-checkconf` on generated files when the binaries exist (skip otherwise, say so) (fixes none; touches F65, F66)
- [ ] Step 12: Type hints on `xg_netpolicy/state.py`, `proxy_logic.py`, `generate.py` public functions; `mypy` would be a new dependency, so ask before adding (fixes none) [APPROVAL for mypy]

## Phase 3: Config and secrets, signatures in data/
- [ ] Step 13: Write `proxy.json` mode 0600 (owner mitmproxy uid 1500), or move `events_token` to env (fixes I-PROXY-13; touches F70, F68) [CROSS: server Step 22]
- [ ] Step 14: Move AI-endpoint signatures (`/async/folwr`, `bing.com`, `GOOGLE_HOST`) to `data/` YAML and load them (fixes I-PROXY-21; touches F69, F70) [CROSS: data/, server Step 26]
- [ ] Step 15: `hide_css` validator rejects `}`, `;` breakouts and `@`; selector allow-pattern (fixes I-PROXY-26; touches F69)

## Phase 4: Structure (moves only)
- [ ] Step 16: Split `xephyguard_addon.py` hook functions into `addons/hooks_tls.py`, `hooks_http.py`, `hooks_shipper.py` (move only, import paths kept) (fixes none; touches F67, F68, F70)

## Phase 5: Duplication and dead code
- [ ] Step 17: Read `network_mode` from the already parsed `State` instead of re-reading the file (fixes I-PROXY-29; touches F64)
- [ ] Step 18: Decide `block_ai_clients`/`exempt_clients` sets: implement a VPN/non-web egress rule or remove the sets and correct F66 text (fixes I-PROXY-18; touches F66) [APPROVAL]
- [ ] Step 19: Correct docs/PROTOCOL.md actions list (`blocked|...|observed`) (fixes I-PROXY-31; touches F70)

## Phase 6: Errors, logging, validation
- [ ] Step 20: `reload` catches every parse error; on failure keep last good policy, and `tls_clienthello` fails closed (block/ignore decision always set) (fixes I-PROXY-15; touches F67, F68)
- [ ] Step 21: Truncate `host` (client Host header) to 255 and drop non-printable characters before queueing events (fixes I-PROXY-11; touches F67, F70)
- [ ] Step 22: `State.from_dict` type-checks every field; run `unbound-checkconf` before reloading Unbound (fixes I-PROXY-19; touches F64, F65)
- [ ] Step 23: Merge/collapse `doh_ips` (`ipaddress.collapse_addresses`) and surface `nft -f` failure as a health flag (fixes I-PROXY-14; touches F65, F66)
- [ ] Step 24: Disjoint-group validation (exempt wins, documented) (fixes I-PROXY-28; touches F64, F65)
- [ ] Step 25: Spill file for failed POSTs is replayed, rotated at a size cap and purged by retention age (fixes I-PROXY-12; touches F70, F67) [CROSS: ETHICS retention]
- [ ] Step 26: De-duplicate `allowed` SNI events per (client, host) per minute; count dropped events in a metric (fixes I-PROXY-30; touches F67, F70)

## Phase 7: Medium/Low bugs
- [ ] Step 27: `observed` events only when `log_urls` is on and within a session; no query/body text (fixes I-PROXY-10; touches F70) [APPROVAL: ETHICS.md:22]
- [ ] Step 28: Normalise request paths (percent-decode, collapse `//`, strip `;params`) before path-rule matching (fixes I-PROXY-16; touches F68)
- [ ] Step 29: Rule hosts are automatically added to the inspection set (`hf.co`, `aws.amazon.com`) (fixes I-PROXY-17; touches F68) [APPROVAL: widens decryption]
- [ ] Step 30: On policy change or isolation, kill open mitmproxy flows for affected clients (fixes I-PROXY-22; touches F67, F36) [CROSS: with Step 4 for nft side]
- [ ] Step 31: `host == "bing.com" or endswith(".bing.com")` (fixes I-PROXY-23; touches F69)
- [ ] Step 32: Category from the matched rule, not hard-coded `"ai"` (fixes I-PROXY-24; touches F68)
- [ ] Step 33: `EventShipper` rebuilt (or updated) when `xg_policy` changes (fixes I-PROXY-25; touches F70)
- [ ] Step 34: `relax_style_csp` adds only the hash/nonce needed for the injected style, or injects via a header-free method; keep site CSP otherwise (fixes I-PROXY-27; touches F69)

## Phase 8: Measured performance
- [ ] Step 35: Micro-benchmark `category_of` and `path_rules_for_group` on 5,000 domains (test with timing budget), then precompute at `reload` (fixes I-PROXY-20; touches F67, F68)

## Phase 9: Dependency updates
- [ ] Step 36: `mitmproxy>=11,<13` in `requirements.txt` matching the image pin (fixes I-PROXY-32; touches F67)
