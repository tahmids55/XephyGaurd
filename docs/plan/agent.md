# Plan: agent/ (C++20 core, Linux/Windows adapters, helper)

Sources: [audit/agent.md](../audit/agent.md) (I-AGENT-1..33), [features/agent/](../features/agent/README.md) (F14, F35, F36, F38, F62, F72-F97).
Plan written 2026-11-17. Nothing built or run while planning.

Rules for every step: one component, <300 changed lines, one kind of change, a test in the same step where one is possible,
then `cmake -S agent -B agent/build -DCMAKE_BUILD_TYPE=Debug && cmake --build agent/build -j2 && ctest --test-dir agent/build --output-on-failure`.
Windows-only code is "compile-checked only" (MinGW `-fsyntax-only`). Steps marked **[APPROVAL]** change a documented feature or an ETHICS item and wait for the owner.
Steps marked **[CROSS]** also touch another component's plan; the agent side lands in the same order as listed in "Cross-component order" at the end.

## Phase 1: Critical/High security and bugs

Parser and crash fixes first (they need no other component):
- [x] Step 1: Safe HTTP number and chunk parsing in `http.cpp` (`from_chars`, overflow-safe cap, per-probe catch in `scan_ports`) + tests with hostile responses (fixes I-AGENT-4, I-AGENT-11; touches F74)
- [x] Step 2: Total deadline (1 s) and 64 KB cap for `http_get_plain` probes (fixes I-AGENT-12; touches F74)
- [x] Step 3: Typed JSON reads and try/catch in `uplink_loop`, `realtime_loop`, command handler, `enroll.cpp`, `discovery.cpp` (fixes I-AGENT-13; touches F14, F35, F38, F72)
- [x] Step 4: Move `apply_policy` state changes (`next_run_`) to a main-loop queue; uplink thread only enqueues (fixes I-AGENT-7 part 1; touches F72, F88) — done via actor core (DECISIONS #22)
- [x] Step 5: Run `rescan`/`kill` from the main loop via the same queue, never off-thread `scan_*` (fixes I-AGENT-7 part 2; touches F35, F37 server side, F87) — done via actor core
- [x] Step 6: One mutex around `ProcessMonitor::cache_` and `known_ports_`/`adapters_` reads from other threads, check with ad-hoc `-fsanitize=thread` run (fixes I-AGENT-7 part 3; touches F73, F74, F82)

Privacy and trust:
- [x] Step 7: `web.ai_tab` rule hit sends `filtered_title` or only `ai_site`, never the raw title (fixes I-AGENT-8; touches F75, F76)
- [x] Step 8: Discovery never supplies the CA fingerprint; require `--ca-fingerprint` or explicit `--trust-on-first-use` (fixes I-AGENT-3; touches F14) [APPROVAL: changes enrollment UX] — done
- [x] Step 9: (revised by DECISIONS #24) Satisfied by screen Step 60: the root service always relays frames over the realtime mTLS WSS client, so there is no plaintext screen path left (fixes I-AGENT-1; touches F95) [CROSS: tick when screen Step 60 is done]
- [x] Step 10: (revised by DECISIONS #24) Stop broadcasting `agent_token` to helpers; the helper gets no gateway token at all, it only hands frames to the service over IPC. Done by screen Step 61 (fixes I-AGENT-6; touches F95, F43) [CROSS: tick when screen Step 61 is done]
- [x] Step 11: Windows: SYSTEM/Administrators-only DACL on `agent.toml` and `agent.key`; helper stops reading `agent.toml` and uses IPC config only (fixes I-AGENT-2; touches F14, F92, F94) (compile-checked only) — done (compile-checked in CI only)
- [x] Step 12: Windows pipe: AU gets read+write-data only, `FILE_FLAG_FIRST_PIPE_INSTANCE`, helper checks `GetNamedPipeServerProcessId` is SYSTEM (fixes I-AGENT-5; touches F90, F93, F94) (compile-checked only) — done (CI only)

## Phase 2: Tooling (lint, warnings, sanitizers)
No new dependencies: compiler flags and sanitizers only. clang-tidy is not added unless you approve it.
- [x] Step 13: Add `-Wall -Wextra -Wshadow` (GCC/Clang) and `/W4` (MSVC) to agent targets; fix only warnings in files already touched, silence none (fixes none; touches F92)
- [x] Step 14: CMake option `XG_SANITIZE=address,undefined|thread`, documented in docs/DEVELOPMENT.md; run ctest once under each and record the result (fixes none; touches F73, F74)
- [x] Step 15: Fuzz-style table tests for `http.cpp`, `protocol.cpp`, `enroll.cpp` JSON parsing using Catch2 generators (no libFuzzer) (fixes none; touches F14, F74, F72)

## Phase 3: Config and secrets, signatures in data/
- [x] Step 16: `write_file_atomic` creates the temp file `O_CREAT|O_EXCL 0600`, fsync, rename; Windows DACL variant (fixes I-AGENT-14; touches F14) — done (POSIX + Windows DACL)
- [x] Step 17: `agent scan` uses `mkdtemp` or `:memory:` queue, never loads unverified `signatures` kv from it (fixes I-AGENT-24; touches F92, F62)
- [x] Step 18: Extend `data/signatures/*.yaml` schema: extension `domains:`, browser `title_patterns:`, process `fallback_domain`, `telemetry_domains`, and teach `SignatureSet` to read them; old hard-coded lists stay until Step 19 (fixes none; touches F62, F76, F80) [CROSS: data/, server SignatureCompiler, compile_signatures.py] — done (data, compile_signatures.py, server, agent)
- [x] Step 19: Replace hard-coded extension domains, AI tab sites, process fallbacks and telemetry domains in `agent.cpp`, `network_monitor.cpp`, `network_win.cpp`, `util.cpp` with the signature data (fixes I-AGENT-19, I-AGENT-20; touches F73, F76, F80, F82) — done
- [x] Step 20: Read `ca_fingerprint` from config and wire `TlsOptions::pin_sha256`, or delete the field (decide with owner) (fixes I-AGENT-31 part; touches F14) [APPROVAL] — done: pin checked by integrity scan, unused leaf pin removed

## Phase 4: Structure (moves only, no logic)
- [x] Step 21: Move HKCU/DisableTaskMgr/WinINet code from `apps/helper_main.cpp` into `platform/windows/helper_system_win.cpp` behind `IHelperSystem` (move only) (fixes I-AGENT-22 part 1; touches F90, F91, F97)
- [x] Step 22: Move gsettings fork/exec into `platform/linux/helper_system_linux.cpp` (move only) (fixes I-AGENT-22 part 2; touches F91, F97)
- [x] Step 23: Move `#ifdef _WIN32` screen-capability defaults and Windows path defaults into `ISystemInfo` (move only; behavior identical) (fixes I-AGENT-32; touches F72, F95)

## Phase 5: Duplication and dead code
- [x] Step 24: Extract `emit_device(d)` and `flow_context(f)` helpers (fixes I-AGENT-33; touches F83, F73)
- [x] Step 25: Remove empty `if`, unused `sys_`; handle or remove `set_indicator` (decide with owner) (fixes I-AGENT-31 part; touches F94) [APPROVAL for `set_indicator`] — done: set_indicator removed (never sent)

## Phase 6: Errors, logging, validation
- [x] Step 26: Rebuild `key_stats` from an allow-list of numeric fields with clamps (fixes I-AGENT-10; touches F78)
- [x] Step 27: Helper sends app names only unless `title_capture=full` was pushed; agent drops suffix otherwise (fixes I-AGENT-9; touches F77, F75)
- [x] Step 28: Per-peer IPC rate limit, message size/type validation, hard queue row/byte cap with oldest-non-tamper eviction (fixes I-AGENT-16, I-AGENT-23 part; touches F85, F77, F78) [update docs/THREAT_MODEL.md in same step]
- [x] Step 29: Netlink proc-connector sender and length checks (fixes I-AGENT-27; touches F73)
- [x] Step 30: Windows pipe: reap finished reader threads, cap concurrent connections at 64 (fixes I-AGENT-28; touches F90) (compile-checked only) — done (CI only)
- [x] Step 31: Apply `max_files`/`max_depth` to the known model-store walk (fixes I-AGENT-29; touches F81)
- [x] Step 32: Reject signature bundles older than the stored version (fixes I-AGENT-30; touches F62)

## Phase 7: Medium/Low bugs
- [x] Step 33: `waitpid(pid, ...)` per launched child in `service_host.cpp` and `x11_helper.cpp` (fixes I-AGENT-15; touches F36, F89)
- [x] Step 34: `helper_terminated` tamper only after the 90 s grace if the session is still active (fixes I-AGENT-17; touches F85)
- [x] Step 35: `detect_ai_tab` matches browser windows only, against Step 19 patterns (fixes I-AGENT-18; touches F76) — done
- [x] Step 36: Replace `popen("nft ...")` with `fork/execv /usr/sbin/nft` and stdin pipe (fixes I-AGENT-25; touches F36, F89)
- [x] Step 37: Resolve CCC with `AF_UNSPEC` and exempt IPv6 CCC addresses in isolation (fixes I-AGENT-26; touches F36, F89)
- [x] Step 38: Default student notice states live screen viewing truthfully (fixes I-AGENT-21; touches F93) [APPROVAL; keep in sync with server I-SERVER-12, docs/ETHICS.md] — done (agent, server texts, ETHICS.md)

## Phase 8: Measured performance
The audit has no performance finding. No step until one is measured.
- [ ] Step 39: Record a baseline with `eval/harness/agent_overhead.sh` (CPU, RSS) before and after Phase 1 and 6; open an I- item only if the numbers regress (fixes none; touches F72, F73)

## Phase 9: Dependency updates
- [x] Step 40: Check that FetchContent pins (nlohmann/json, toml++, Catch2) are exact tags; report only, ask before updating (fixes none; touches none) — pins exact (json URL+SHA256, toml++ v3.4.0, Catch2 v3.7.1 tags); nothing updated

## Cross-component order (agent side)
1. screen/ccc-deploy: gateway behind nginx mTLS (I-CCC-1, I-CCC-7) → agent Step 9.
2. screen Steps 59-61 (helper→service IPC, service relay, helper drops its WS client) → agent Step 10; server Step 1 (`pc_id`-bound viewer JWT, I-SERVER-1) is independent of the agent.
3. data/ signature schema (Step 18) → agent Step 19 → Step 35.

## Alternative architecture proposals (not scheduled; each needs owner approval before any step is written)

Overall proposal: **single-threaded actor core**. Collectors (process, ports, window, USB, adapters, artifacts) run as small producers that push typed messages into one bounded queue; one core thread owns all state (`known_ports_`, `adapters_`, `next_run_`, process cache), applies policy, talks to the event queue and uplink. This removes the whole data-race class behind I-AGENT-7 by construction instead of by mutexes, and makes the agent testable by feeding messages. The uplink/realtime threads only do I/O and enqueue. Steps 4-6 are the incremental path toward it.

| Feature | Current | Better alternative | Why / cost |
|---|---|---|---|
| F14 Enrollment | CSR to CCC; fingerprint can come from UDP discovery | Install package carries the CA fingerprint and a one-time per-PC token; enrollment is CSR + token (EST-style); discovery gives only a hostname hint | Removes the rogue-LAN-host path; needs installer change |
| F35 Kill process | PID from CCC, guards on agent | Send PID + process start time; agent verifies against its own snapshot; use `pidfd_open` on Linux | Prevents PID-reuse kills; small |
| F36 / F89 Isolation | Agent runs nft via shell string; Windows partial | Gateway-side isolation is the primary (CCC `isolated` set, already exists); agent rules are a backup via libnftables-free `execv` / Windows WFP | One enforcement point that a student cannot remove; no shell **Proxy mode (current focus): the agent rules are the primary enforcement, not a backup.** Done: `execv` nft, session network lock (DECISIONS #23) |
| F38 Ping/flush | Command | Keep | Simple |
| F62 Bundle verification | ECDSA over bundle | Sign `{version, issued_at, expires_at, sha256}` and keep a monotonic version (TUF-lite) | Fixes rollback (I-AGENT-30) and stale bundles |
| F72 Heartbeat | JSON, hand-read fields | Same wire format; parse into typed structs with one `from_json` validator per message | Fixes I-AGENT-13 class |
| F73 Process monitoring | Netlink proc connector + 2 s `/proc` diff | Keep; feed through the actor queue. eBPF not worth the kernel/dependency cost on lab PCs | Matches DECISIONS #13 |
| F74 Ports / LLM probe | Blocking probe in main loop | Non-blocking connect with a 1 s budget on a worker, results enqueued; probe only ports matching signature hints | Removes stall risk (I-AGENT-12) |
| F75 Window titles | Raw title, filtered by `title_capture` | Default to application identity only; title never leaves the PC unless `full` | Matches ETHICS defaults |
| F76 Browser AI tab | Substring match on window title | Make the proxy's SNI/domain decision (F67) the authority; title heuristic only as a low-weight corroboration; optional managed browser extension reporting a matched domain only | Fewer false positives (I-AGENT-18) |
| F77 Paste/copy | Size only | Keep | Privacy-safe |
| F78 Keystroke timing | Timing stats | Compute fixed histograms in the helper; service accepts only a numeric schema | Closes I-AGENT-10 by design |
| F79 File activity | inotify/ReadDirectoryChangesW, status unclear | Send counts and byte deltas per directory, not file names, unless Test mode | Less sensitive data |
| F80 Extensions | Inventory and activity | Also push managed-browser policy (block list) so detection is backed by enforcement where F91 is on | Detect and prevent in one data source |
| F81 Model files | Walk known stores | Same, with budget (Step 31) | Keep |
| F82 Adapters/VPN | Polling | Subscribe to `RTM_NEWLINK/NEWADDR` netlink (Linux) / `NotifyIpInterfaceChange` (Windows) | Instant, cheaper |
| F83 USB | Raw uevents | Keep | Already minimal |
| F84 GPU | Sampling | Keep; optional | Low value |
| F85 Tamper | Agent self-reports | CCC silence detection (F108) is the source of truth; add systemd `WatchdogSec` / Windows service recovery; agent heartbeat carries a monotonic counter | Tamper evidence survives agent kill |
| F86 Raw capture (Windows) | Packet capture | Drop in favour of gateway observation (F66/F70) unless a no-gateway mode is needed | Less privileged code. **Keep: proxy mode is the no-gateway mode** |
| F87 / F88 Policy kill / re-evaluate | In agent | Keep, via actor queue | Simple |
| F90 Exam lockdown (Windows) | Agent code | Deliver as a Windows policy bundle (AppLocker/registry) applied by the service from signed data | Less custom code, auditable |
| F91 Browser policies/DNS | Agent writes settings | Keep; write via `IHelperSystem` / adapter only | Matches invariant |
| F92 CLI/service | Binary | Keep | |
| F93 Notifications | Helper | Keep; notice text comes from signed policy | |
| F94 Status indicator | Helper | Keep (decide `set_indicator`, Step 25) | |
| F95 Screen streamer | Helper streams to :8082 with shared token | Helper captures; the **root service** relays frames to the CCC over the existing mTLS channel on :8443 (nginx path `/screen-gw/`), per-view JWT bound to `pc_id` | One mTLS path, no token in user space; needs gateway change |
| F96 Helper status | Command | Keep | |
| F97 PAC/proxy | HTTP PAC | PAC served over HTTPS from the lab CA, no `DIRECT` fallback in Test mode (server I-SERVER-24); no PAC if a direct system proxy setting is enough | Closes LAN tamper |

## Proxy-mode additions (done 2026-11-17, DECISIONS #23)

- Session network lock, chosen per session on the Sessions page (proxy-only mode only): `observe` (default) drops
  only QUIC so browsers fall back to TCP through the proxy; `strict` drops every public destination except the CCC;
  `off`. Own nft table `xephyguard_lock` / firewall rule `XephyGuard Lock`; re-applied + `tamper/network_lock_removed`
  if removed; heartbeat reports the installed lock; removed on uninstall.
- Tunnel flags `net.tunnel_flow` during sessions: TCP to a public host on ports from `web.yaml` `tunnel`
  (SSH, OpenVPN/TCP, PPTP, SOCKS, Tor). Destination, port and program only; never closed automatically.
- Not run: the nft / netsh rules on a real PC (needs root / Windows).

## Screen work lives in plan/screen.md
The embedded streamer (F95, `agent/apps/screen_streamer.cpp`) is planned in [screen.md](screen.md) because smoothness work spans the gateway, viewer and streamer. Agent-component steps there: S-11 (privacy filter), S-14 (honour mode), S-15 (start by signed lease), S-16 and S-46 (Windows socket), S-34 (cursor). Steps 9-10 above must land after screen Steps 1-2 and server Step 3.
