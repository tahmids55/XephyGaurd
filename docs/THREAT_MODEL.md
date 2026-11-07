# Threat model

**Assets:** fairness of lab tests; the evidence trail (events, alerts, audit log); students' privacy;
the lab CA private key; agent credentials.
**Adversary:** a student with a standard (non-admin) account on a lab PC, unlimited time to prepare,
personal devices, and knowledge of this document. Out of scope: lab administrators acting maliciously,
physical attacks on the CCC, nation-state adversaries.
**Goal of the system:** make AI use during tests *hard and visible*, not impossible (spec non-goals).

Columns: **Prevented** = blocked by design; **Detected** = recorded and alerted; **Residual** = what remains.
Red-team scenario ids refer to `eval/scenarios/*.yaml`; measured outcomes go in `eval/results/`.

| # | Bypass attempt | Prevented by | Detected by | Residual risk | Scenario |
|---|---|---|---|---|---|
| 1 | Open a chatbot / LLM API | DNS sinkhole (Unbound view) + SNI filter at the gateway | blocked attempt logged (net_events), counted in score | new AI domains not on the list → keep lists updated, import community lists | web.*, api.openai |
| 2 | Google AI Overview / Bing Copilot | `udm=14` rewrite + CSS fallback on inspected hosts; Bing chat path rules | `search-ai rewritten` events | markup changes; PCs without the lab CA get no inspection → `live_checks/google_ai_overview.py` weekly | search.ai_overview |
| 3 | Change browser/system proxy settings | gateway mode: traffic is redirected regardless of PC settings | – | proxy-only mode is weaker (documented) | bypass.proxy_env |
| 4 | VPN / Tor / web proxy | strict egress in Test (only 80/443 via proxy); VPN/Tor/proxy domains blocked | VPN client processes, tun/wg adapters (`net.vpn_*`) | VPN over TCP 443 to an unlisted IP passes the SNI layer if it mimics TLS with a benign SNI | bypass.vpn_tun, bypass.tor |
| 5 | DoH / DoT / public DNS | DNS DNAT to the CCC; resolver IPs and port 853 dropped; DoH hostnames sinkholed; browser policy `DnsOverHttpsMode=off` | blocked attempts | private DoH on an unknown host:443 | bypass.doh, bypass.dot, bypass.public_dns |
| 6 | QUIC / HTTP/3 | UDP 443 dropped, browsers fall back to TCP | – | – | bypass.quic |
| 7 | Edit hosts file | needs admin; DNS is not needed if IP is used, but SNI filter still sees the name | blocked SNI event | IP-only HTTPS without SNI is refused in Test mode | bypass.hosts_file |
| 8 | Local model (Ollama, llama.cpp, LM Studio) | optional `kill` action per profile | process/port/artifact rules, GPU spikes | renamed binaries → port probe (`/v1/models`, `/api/tags`), artifact scan, GPU heuristic | local.* |
| 9 | Local model inside WSL / VM / container | lab hardening (disable WSL/Hyper-V, Docker restricted) | `vm.*` rules, GPU spike | a VM started before the session is reported at session start (re-evaluation) but its contents are opaque | local.container |
| 10 | AI IDE extension / CLI agent | marketplaces blocked in Test; root-owned extensions dir; AI backends blocked | extension scan, process rules (incl. node/python command lines) | offline/self-hosted assistants → covered by local-model rules | ide.*, cli.* |
| 11 | Portable AI editor from USB | AI backends blocked | process rules, `dev.usb_storage` event | renamed portable binaries | usb.portable_editor |
| 12 | Phone hotspot / USB tethering | – (second route bypasses the gateway) | `net.tether_usb`, `net.wifi_new`, `net.second_route` alerts | a phone used *without* connecting it to the PC is invisible (stated in every report) | behavior.phone_hotspot (manual) |
| 13 | Copy answers from a phone / second screen | – | paste and typing-rhythm signals (idle-then-burst, transcription without corrections) | typing an answer read from a phone looks like normal typing → behavioral score only | behavior.* |
| 14 | Kill/stop the agent | service runs as root/SYSTEM; SCM/systemd restart; users cannot stop it | clean stop → `tamper.agent_stopped`; kill → `machine_silent` (≤ 15 s) + `unclean_restart` | – | tamper.stop_agent |
| 15 | Kill/mask the session helper | helper is relaunched (systemd user unit / CreateProcessAsUser) | `tamper.helper_missing` after 90 s | window/clipboard data missing during the gap (key timing still from root on Linux) | tamper.kill_helper |
| 16 | Change the clock | needs admin; CCC is the time authority | `tamper.clock_change` (wall vs. monotonic) | – | tamper.clock |
| 17 | Remove/replace the lab CA | needs admin | `tamper.ca_removed/ca_changed` | – | – |
| 18 | Boot another OS from USB | BIOS password + boot order (lab hardening, document) | PC goes silent → `machine_silent` alert | a live OS on the lab network has no agent → gateway still blocks AI domains; enrollment needed to look "clean" | – |
| 19 | Spoof a PC / replay agent traffic | mTLS + per-PC token + cert fingerprint binding; seq de-dup | invalid/foreign batches rejected (422/403) | a stolen PC key *and* token (root on the PC) → revoke via Retire | – |
| 20 | Spoof IP to get another PC's (exempt) policy | DHCP reservations, switch port security (recommended) | duplicate IP shows as two PCs reporting the same IP | per-IP network policy relies on IP integrity on the lab LAN | – |
| 21 | Tamper with signatures pushed to agents | bundles signed with the CA key, verified on the PC | `tamper.signature_invalid` | – | – |
| 22 | Attack the CCC from the lab LAN | only nginx is exposed (80/443/8443); internal APIs on 127.0.0.1; login rate limit; CSRF; roles enforced server-side; append-only audit log | audit log | keep the CCC patched (`xephyguard-ccc update`) | – |

## Trust assumptions
* Lab users are **standard users** (no sudo / local admin). With admin rights every agent-side
  control can be disabled; the gateway layers still apply.
* The CCC host and its CA key are trusted. Backups (`xephyguard-ccc backup`) contain the CA key.
* The mitmproxy container holds the CA key to mint interception certificates (decision 11).

## Privacy threats (abuse of the monitoring itself)
* Over-collection → tiered modes, title capture off/app-only by default, numeric reduction on the PC.
* Function creep / misuse of reports → reports say "flag for human review"; no automatic sanctions;
  audit log of every view-changing action; retention purge; research exports only with consent and
  pseudonymized (see `ETHICS.md`).
