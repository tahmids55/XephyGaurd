# XephyGuard — commit history review

Read-only review of the git history, oldest first, in batches of 10 commits. No code was changed
while writing this. Lockfiles, build output and generated files were skipped.
"Status at HEAD" refers to commit `4ac2d8e` (branch `taki`, 2026-11-17).

Batch 1 was reviewed directly. Batches 2–7 were reviewed in parallel by separate read-only review
passes and merged here in commit order; their most serious claims were re-checked against HEAD
before merging (see "Overall summary" at the end). Secret values that appear in history (a PFX
password, a personal email address) are redacted here on purpose.

Legend: **[SEC]** security, **[PRIV]** privacy / ETHICS.md, **[INV]** breaks a CLAUDE.md invariant or
working rule, **[UNDO]** undoes earlier work, **[HACK]** shortcut / dev convenience left in.

---

## Summary

### How the project evolved
1. **Nov 1: one big import.** The whole system lands in a single 35k-line commit: agent,
   CCC server, proxy, analysis, deploy, installers, eval, docs. It is a clean base: no TODOs, no
   committed secrets, strict mTLS, privacy rules written down in ETHICS.md.
2. **Nov 3: setup and install fixes.** A second setup guide at the repo root and `install.sh`
   hardening.
3. **Nov 7: a governance phase, then a rollback.** Two large commits add Phase 0–4 work (appeals,
   student portal, two-person approval, ethics pack, fairness analysis, ethics tests).
   `04a0d3b` throws almost all of it away as a "revert to stable base" and adds new features in
   the same commit.
4. **Nov 8–11: monitoring gets much wider.** Process, USB, port and adapter scans, browser tab
   tracking, extension "activity", network isolation, kill-by-default in tests, Task Manager
   lockout, copy/paste file detection, raw packet capture on Windows, a public shared clipboard.
   Scan intervals drop from minutes to seconds.
5. **Nov 11: a whole screen-streaming subsystem in one day** (`xephyguard-screen/`, WP0–WP6):
   capture, a Go gateway relay on :8082, a browser viewer and a standalone agent. It is built
   beside the mTLS channel, not through it, and against ETHICS.md's "no screen content" rule.
6. **Nov 12–13: streaming merged into the real agent.** It goes into `xephyguard-helper`, then
   come Xerophyte Labs branding, a self-signed code-signing cert installed as a root CA, a
   committed `gateway.exe`, and a large "remove hard-coded config" clean-up plus CI fixes.
7. **Nov 13–14: proxy, CA and signature plumbing.** mitmproxy CA handling, proxy-only mode, AI
   Overview suppression, and signature bundles that auto-publish every minute.
8. **Nov 14: UI v2.** First as static HTML mock-ups that replace working views (fake login, fake
   data, viewer code deleted), then rewired to the backend over the next ~10 commits; Windows
   NSIS packaging in CI.
9. **Nov 15: the latest work.** The enrollment key goes back on the public downloads page,
   allowed AI traffic is now logged, Linux audit and X11 screen fixes land, and server-side
   screenshot "evidence" arrives under a subject that does not mention it.

**Overall direction:** a privacy-first, block-and-flag tool with strict mTLS turned step by step
into a broad surveillance tool (live screens, screenshots, window titles, keystroke timing,
packet capture) with several side channels outside mTLS. Its own docs were never updated to
match.

### Added, then removed or broken later
| Feature | Added | What happened |
|---|---|---|
| Appeals, student portal, two-person gate, ethics pack, fairness analysis, ethics contract tests, roadmap/charter docs | `94af603`, `a8124dc` | Deleted by `04a0d3b`; none came back. |
| Strict "own channel only" Reverb auth for agents | initial | Loosened in `04a0d3b` (any `private-pc.*`), fixed in `c9464b0`. |
| Commands delivered to one PC only | initial | `04a0d3b` fanned out to same-hostname PCs; fixed in `37c96ae`. |
| Alerts for local model files and installed AI extensions | initial | Silently moved to an "Installed" tab in `0a483a2`; no decision recorded. |
| Process/extension/artifact scan history | initial | `c9464b0`, `0a483a2` delete all but the newest scan. |
| Working dashboard views, login, live lab, Reverb/ScreenViewer wiring | initial → `e1be056` | Replaced by static mock-ups in `5269f30`–`5681d2d`; mostly rewired by `e7e9af7`. Some viewer fixes (e.g. `31594cf`) were lost. |
| HTML session report with signal IDs, review comments, "not observable" column | initial | Dropped in `53e7d8e`; HTML and PDF now show different scores. |
| Full student notice (window titles, blocked sites, research, retention) | initial | Cut down in `5269f30`; now also claims screens are never recorded, which is false. |
| Google AI Overview suppression on by default | earlier profiles | Default flipped to off in `de47069` for profiles saved without the box. |
| Windows unit tests in CI | initial CI | Dropped in `964b4a7`; still off. Helper build dropped too, re-added in `80bfcbf`. |
| CSP-compatible pages | — | Inline scripts in `31fc124` broke under the nginx CSP; fixed in `d9dc579`. |

### Started but never finished
- **Screen privacy masking.** `PrivacyFilter` exists with tests, but nothing outside the tests
  turns it on; exam mode turns masking off anyway.
- **On-screen indicator.** `ScreenIndicator` is a bool that draws nothing, even though the
  docs call it a "persistent indicator".
- **X11 dirty-rectangle capture.** The XDamage object is created and never read; every frame is
  copied in full.
- **Helper extension IDs.** `ext_ids` are sent to the helper by `7216fa3`, and nothing reads them.
- **`insecure_skip_verify`.** A TLS option that nothing sets.
- **Screen channel security.** TLS is a config flag that is off by default, auth is one shared
  token, `stream_id` is chosen by the client, and the Origin check is off.
- **Health classification.** `AgentHealthClassifier` labels PCs `SUSPECTED_TAMPERING` without
  traceable reasons.
- **Windows support.** Adapters, helper, PAC/registry code and installer are compile-checked only
  and were never run on Windows.
- **Gateway tests.** `relay_test.go` exists but no CI job runs it; `pint` lint fails on about 60
  files and is set to `continue-on-error`.
- **Docs the rules depend on.** `docs/FEATURES.md` and `docs/PLAN.md` are referenced by
  CLAUDE.md but were never created; ETHICS, PROTOCOL, DECISIONS and THREAT_MODEL never caught up
  with screen streaming.
- **Leftover demo data.** `public/js/data.js` (fake students and alerts) and a fake-desktop drawer
  are still served.

### Recurring problems (patterns that kept repeating)
Tag counts across all 65 entries: **[SEC] 98, [INV] 88, [HACK] 63, [PRIV] 45, [UNDO] 16.**
1. **New features collect more data without an ETHICS.md check.** Screen content, screenshots,
   window titles, keystroke timing, clipboard metrics, file names, packet capture and allowed-AI
   URLs were each added in a commit that never touched ETHICS.md.
2. **Side channels that skip mTLS.** The screen gateway (:8082 `ws://`), `/share`, `/downloads`
   with the enrollment key, and `curl http://… | sudo bash`. The core :8443 mTLS stays strict,
   but new features route around it.
3. **Public or weakly checked endpoints.** `/share`, `/downloads`, `issuePcToken` with no
   ownership check outside sessions, and batch-acknowledge with no lab scoping.
4. **Hard-coded detection data.** IDE/browser exe lists, AI domains and process names in agent
   core, the Linux adapter and Blade fallbacks, instead of `data/` (about 47 mentions in this
   review).
5. **Commit messages that hide what changed.** Features labelled "tests" (`a8124dc`), a revert
   that also adds features (`04a0d3b`), "Restore" with nothing to restore (`e9ded61`), and
   screenshot storage inside "Linux audit fixes" (`4ac2d8e`).
6. **Huge cross-component commits.** Agent, server, installer and docs together, often 50–115
   files, against the "one small step, one component" rule.
7. **Protocol and design changes without docs.** New commands, policy fields and a whole new
   channel, with `docs/PROTOCOL.md` or `docs/DECISIONS.md` updated almost never (18 mentions).
8. **Build artifacts and secrets in git.** `gateway.exe`, `.cer` files, a PFX password and a
   personal email address in demo data, plus a dev JWT secret baked into a binary.
9. **Dev conveniences left in production.** Test-command presets, fake data, debug logging of
   cert fingerprints, `php artisan serve --host=0.0.0.0`, auto-publish with no review.
10. **Tests and CI weakened quietly.** Windows ctest dropped, the gateway Go tests never run,
    `pint` failures ignored, and ethics tests deleted with the governance phase.
11. **Rewrite instead of edit.** Whole files or views replaced (UI v2 mock-ups, `app.js`
    overwritten with demo data), which keeps losing earlier fixes.

---

## Batch 1 — commits 1–10 (2026-11-01 → 2026-11-08)

### 1. `51649d2` Initial commit: XephyGuard lab AI-use control system
- **Intent:** import the whole system in one go (347 files, ~35.7k lines): C++20 agent + session
  helper with Linux/Windows adapters and Catch2 tests, Laravel 13 CCC (dashboard, agent API, Reverb,
  internal CA, reports), mitmproxy addon + Unbound + nftables, Python analysis (v1 rules, v2
  IsolationForest), Docker Compose deploy with nginx mTLS front, installers (.deb, NSIS, GPO,
  Ansible, browser policies), eval harness, docs.
- **Touched:** everything. Largest areas: `server/app` (71 files), `agent/core` (45),
  `agent/platform` (25).
- **Problems:**
  - [SEC][INV] `agent/core/src/tls.cpp:153` — `TlsOptions::insecure_skip_verify` switches to
    `SSL_VERIFY_NONE` and skips hostname checks (`:178`). Nothing sets it (the header comment says
    "only for `--insecure` diagnostics"), so it is dead code today, but it is exactly the
    "disable cert verification" path CLAUDE.md forbids. **Still present at HEAD.**
  - No TODO/FIXME markers and no committed keys, certs or `.env` files found.
  - One huge commit: there is no history for how the base design evolved.

### 2. `82b7540` Add complete lab setup guide to README and create guide_setup.md
- **Intent:** step-by-step lab setup guide (server, agent install, acceptance tests).
- **Touched:** `README.md` (+3), new `guide_setup.md` (784 lines).
- **Problems:** a second setup guide at the repo root, next to `docs/SETUP_*.md` and
  `docs/RUNNING_THE_SERVER.md`, so the instructions are duplicated and can drift apart.

### 3. `52105e3` Fix network interface detection and improve error handling in install script
- **Intent:** make `ccc-deploy/install.sh` survive `set -e`/`pipefail` when there is no default
  route, and fix permissions.
- **Touched:** `ccc-deploy/install.sh` only.
- **Changes:** `|| true` on interface/route/IP lookups; `[ ] && echo && break` rewritten as
  `if/then` (the old form returned non-zero and could abort the script); `systemctl restart docker`
  after the systemd-resolved stub is disabled (dockerd caches `resolv.conf`); `net/`, `storage/` and
  `/var/log/xephyguard` now owned by uid 33 (www-data).
- **Problems:** none serious. uid 33 is hard-coded (correct for the Debian-based PHP image only).

### 4. `94af603` feat: Add roadmap documentation and implement Phase 0 policy synchronization
- **Intent:** start a phased roadmap. Add `ROADMAP.md`, `CHARTER.md`, extended `ETHICS.md`, a
  Phase 0 blocklist/rules migration + seeder, `Phase0RegressionTest` and `EthicsContractTest`,
  big `EventIngestor` (+271) / `PolicyBuilder` (+210) / `agent.cpp` (+554) changes, NSIS rework,
  `LICENSE` (AGPL-3.0 text), `SECURITY.md`.
- **Touched:** 50 files across agent, server, installers, docs.
- **Problems:**
  - [HACK] Adds `start_server.bat` and `sync_network.py` at the repo root (Windows-only server
    starter and an IP/SSL "sync" script).
  - [INV] Mixes agent, server, installer and docs work in one commit (breaks "one component per
    step").
  - **Almost all of it is deleted 3 commits later by `04a0d3b`** (only `LICENSE`, `SECURITY.md` and
    part of the NSIS changes survive).

### 5. `a8124dc` Add comprehensive tests for Phase 2 to 4 workflows …
- **Intent:** despite the "tests" title, this is a large feature commit (80 files, +6.3k): student
  transparency portal, appeals, two-person approval gate (`TwoPersonGate`), ethics pack generator,
  retention purge/backup/restore commands, enrollment keys, health endpoint, fairness analysis
  (`analysis/xg_analysis/fairness.py`), Bengali + English translations, agent `crypto.cpp`,
  SBOM script, accessibility and pilot-study docs, plus 900 lines of Phase 1–4 tests.
- **Problems:**
  - The commit message hides most of what changed (features are labelled as tests).
  - [INV] New migrations (`…000700_create_enrollment_keys_table`, `…000800_create_appeals_…`)
    and cross-component changes in one step.
  - **Fully deleted 2 commits later by `04a0d3b`.**

### 6. `bbaacc4` test: add standard unittest wrapper to eval harness suite
- **Intent:** let `eval/tests/test_harness.py` run under plain `unittest`, not just pytest.
- **Touched:** `eval/tests/test_harness.py` (+18).
- **Problems:** [HACK] copy-paste wrapper that calls 4 of the 5 pytest functions again; under
  pytest those tests ran twice (once directly, once through `EvalHarnessTests`). Needs a late
  `import unittest  # noqa: E402`.
- [UNDO] The wrapper and `test_pilot_governance_summary_and_fairness` (added in `a8124dc`) were both
  removed by `04a0d3b`. HEAD has 4 plain pytest tests and no wrapper.

### 7. `04a0d3b` revert to stable base with deduplication, modal popups, and downloads portal
- **Intent:** throw away commits 4–5 ("revert to stable base") and, in the same commit, add new
  features: duplicate-PC handling on enrollment, modal popups on Windows, a public `/downloads` page.
- **Touched:** 115 files, +503 / −8,916.
- **What it undid** [UNDO]: everything from `94af603` and `a8124dc` except `LICENSE`, `SECURITY.md`
  and the NSIS changes. Deleted, among others: `Appeal`, `TwoPersonGate`, `EthicsPackGenerator`,
  `StudentPortalController`, `PurgeRetentionCommand`, `BackupCommand`/`RestoreCommand`,
  `EnrollmentKey`, `HealthController`, `fairness.py`, `EthicsContractTest`, all Phase 0–4 tests,
  `docs/ROADMAP.md`, `docs/CHARTER.md`, `docs/PILOT_STUDY_PROTOCOL.md`,
  `docs/ACCESSIBILITY_AND_ACCOMMODATIONS.md`, both translation files and 3 migrations.
  **None of these are back at HEAD.** (Retention purge still exists separately as
  `server/app/Jobs/PurgeRetention.php` from the initial commit.)
- **New code and its problems:**
  - [SEC] `AgentController::broadcastAuth` — the check "channel must be `private-pc.<own pc_id>`"
    was loosened to "channel starts with `private-pc.`". **Any enrolled agent could subscribe to
    every other PC's command channel.** Fixed in the next commit (`c9464b0`).
  - [SEC] `Events/AgentCommand::broadcastOn` — commands were also broadcast to every other PC with
    the same hostname. Hostnames are not unique or trusted, so commands (kill, isolate, message …)
    could reach the wrong machine. Removed in `37c96ae`; HEAD has a comment saying one channel per PC.
  - `EnrollmentService::approve` — re-enrollment now matches by MAC **or hostname**, restores retired
    PCs, merges MAC lists, retires every other active PC with the same hostname and supersedes
    other pending enrollments. Two different lab PCs with the same hostname would overwrite each
    other's record and history (teacher approval limits the damage). The MAC/hostname match closure
    is copy-pasted twice. Early return when already approved makes approve idempotent (good).
  - `AuthenticateAgent` / `broadcastAuth` now log certificate fingerprints and requested channels
    on failure (debug logging left in; fingerprints are not secret, but it is noise in a hot path).
  - Windows helper `notify()` now also opens a `MessageBoxW` on a detached thread for **every**
    notification, `MB_SYSTEMMODAL` when urgent. Repeated notifications stack up blocking dialogs;
    detached threads are never joined.
  - Deleted migrations leave orphan rows in the `migrations` table and orphan tables on any database
    that had already run them.
  - [INV] A revert and new features in one commit, across agent + server.

### 8. `c9464b0` feat: add AI editor process signature and enhance process management
- **Intent:** detect Cursor, sort/filter the running-process list on the dashboard, add a `rescan`
  command, fix CA key generation on hosts without a default OpenSSL config.
- **Touched:** agent process tracking (Windows `process_monitor_win.cpp` +310), `SessionController`
  (`rescan`), `DashboardController`, `EventIngestor`, `CertificateAuthority`, `app.js`,
  `live.blade.php`, tests.
- **Fixes:** restores the strict `broadcastAuth` channel check broken in `04a0d3b`.
- **Problems:**
  - [INV] New agent↔CCC command `rescan` (`what` = processes|extensions|artifacts|all) without a
    `docs/PROTOCOL.md` update.
  - `EventIngestor` now deletes every older `process_snapshot` for the PC on each batch (only the
    newest is kept). Saves space but removes history a reviewer might want when checking a flag.
    The delete is keyed on `seq`, which is per agent: after a re-enrollment the old agent's
    snapshots with higher `seq` values are never cleaned up.
  - `CertificateAuthority::opensslConfig()` changed from private to public just so tests can use it.

### 9. `0290fc7` feat: Enhance device monitoring and network scanning capabilities
- **Intent:** USB device scanning (Linux + Windows), IPv6-safe port scanning, listening ports and
  network adapter events, dashboard panels for them, more alerts shown (15 → 30), and a
  "shared clipboard" page.
- **Touched:** agent device/network adapters, `data/signatures/devices.yaml`, `DashboardController`,
  `EventIngestor`, `PublicController` (+158), routes, `live.blade.php`.
- **Problems:**
  - [SEC][PRIV][HACK] **`/share`, `POST /share`, `/share/raw` — an unauthenticated, network-wide
    text pad.** Anyone who can reach the CCC web port (including students on lab PCs) can read and
    overwrite it; it polls every 2.5 s. On a system meant to stop cheating this is a ready-made
    channel for passing answers between seats, and it stores arbitrary free text on the server,
    which conflicts with the "no messages / no text" spirit of ETHICS.md. It ships with "quick test
    commands" that start fake LLM servers on ports 11434/8080, so it looks like a dev convenience
    tool. Only CSRF protects `POST`, and CSRF does not stop a student with a browser.
    **Still present at HEAD**, now with a history list and an extra `/share.html` route
    (`server/routes/web.php:27-30`, `PublicController.php:~190-205`).
  - The whole page is an inline HTML heredoc in a controller instead of a Blade view.
  - No commit message mention of the share feature's security model; not in any doc.

### 10. `0a483a2` feat: Enhance AI extension monitoring and artifact detection with live activity tracking
- **Intent:** only alert on AI extensions that are actively in use; move installed-only extensions
  and on-disk model files to an "Installed" tab; scan much more often.
- **Touched:** `agent.cpp` (+102), `artifact_scan.cpp`, `policy.cpp/hpp`, `util.cpp`
  (`APPDATA`/`LOCALAPPDATA` fallbacks), Windows `extensions_win.cpp`, `artifacts.yaml`,
  `SignatureCompiler`, `EventIngestor`, dashboard.
- **Behaviour changes:**
  - Local model files (`*.gguf`, Ollama/LM Studio stores) no longer raise alerts at all; the agent
    only emits `artifact_scan` and the server drops `local_model` / `file.*` hits from Alerts.
    The spec (`XephyGUARD_PROJECT_SPEC.md:90`) only requires a scheduled scan, so this is allowed,
    but it is a user-visible behaviour change that is not written down in `docs/DECISIONS.md`.
  - Installed-but-inactive AI extensions no longer alert either.
  - Default intervals: snapshot 60 → 15 s, extension scan 300 → 15 s (policy floor 30 → 10 s),
    first artifact scan delay 60 → 5 s after start. Much more load on lab PCs and on the CCC.
- **Problems:**
  - [INV] **Hard-coded IDE/browser executable lists in the agent core** (`ide_is_running`: `code`,
    `cursor`, `idea64`, `pycharm64`, `chrome`, `msedge` …). CLAUDE.md: signatures live in `data/`,
    never hard-coded in the agent. **Still present at HEAD** (`agent/core/src/agent.cpp:~526-552`).
  - The "active" check is weak: any process whose path/cmdline contains the extension id and has a
    non-empty command line counts as active (`|| !p.cmdline.empty()`), so the CPU/network tests
    are effectively bypassed.
  - `EventIngestor` now also deletes older `extension_scan` and `artifact_scan` events — copy-pasted
    block, three times, same per-agent `seq` issue as in `c9464b0`.

### Batch 1 summary
1. **Unauthenticated `/share` text pad (`0290fc7`) is still live at HEAD** — security and ethics
   problem, highest priority.
2. **`04a0d3b` silently deleted the appeals, student portal, two-person gate, ethics pack,
   fairness analysis and the ethics contract tests**, and none of it came back. The ethics docs
   may still describe some of these features.
3. Two command-routing holes from `04a0d3b` (any agent could subscribe to any PC's channel;
   commands fanned out by hostname). Both fixed later (`c9464b0`, `37c96ae`).
4. Hard-coded IDE/browser lists in the agent core (`0a483a2`), still at HEAD.
5. Dead `insecure_skip_verify` TLS option since the initial commit, still at HEAD.
6. Process general: features labelled as tests, reverts mixed with features, cross-component
   commits, protocol changes without `docs/PROTOCOL.md`. `docs/FEATURES.md` and `docs/PLAN.md`,
   which CLAUDE.md refers to, do not exist.

---

## Batch 2 — commits 11–20 (2026-11-08 → 2026-11-11)

### 11. `f599f4d` Enhance AI monitoring and browser tab tracking
- **Intent:** Detect live AI use from outbound network traffic (Windows DNS cache + raw-socket TLS SNI sniffing + per-socket TCP EStats byte counters), flag AI browser tabs from window titles, and show both on a new "browser" tab in the dashboard drawer.
- **Touched:** agent/core (agent.cpp `scan_ai_traffic`, `scan_extensions`, `on_helper_message` window handling, types.hpp `OutboundAiFlow`, interfaces.hpp), agent/platform/windows (network_win.cpp, extensions_win.cpp, helper_win.cpp), server (DashboardController, Bootstrap profiles, PolicyBuilder, live.blade.php), tests. Crosses agent and server in one commit.
- **Problems:**
  - [PRIV][INV] PolicyBuilder now ignores the profile's `title_capture` and defaults every mode to `'full'` (`Setting::get("privacy.title_capture.$mode", 'full')`). Bootstrap sets all five profiles, Always-on included, to `title_capture => 'full'` and `collect.window => true`. docs/ETHICS.md says Always-on collects no app name and no titles, and Practice defaults to "off / app name". Full window titles can hold document names, chat titles and e-mail subjects. Still present at HEAD (`server/app/Services/PolicyBuilder.php:58`, `Bootstrap.php:17-29`).
  - [PRIV] `web.ai_tab` rule hits put the raw, unfiltered window title in the context (`{"title", limit(raw_title, 256)}`), which skips `pol.filter_title`. The title leaves the PC even when title_capture is `app_only`/`off`. Still present at HEAD (`agent/core/src/agent.cpp:822`).
  - [PRIV][SEC] network_win.cpp opens a raw `SOCK_RAW` socket with `SIO_RCVALL` and reads every IP packet on the default interface, as SYSTEM, to pull SNI out of TLS ClientHellos. This is full packet capture. It is not in docs/ETHICS.md, docs/THREAT_MODEL.md or DECISIONS.md. The parser is bounds-checked, but it handles untrusted bytes from the whole LAN path. Still present at HEAD (`agent/platform/windows/network_win.cpp:535`).
  - [HACK] The `NetworkMonitor` constructor silently writes HKLM browser policies (turns off Chrome/Edge/Brave DoH and the built-in DNS client, locks Firefox DoH off) on every agent start, in every mode, and never undoes them. Changing system config is a side effect of a "monitor". Still present at HEAD (`network_win.cpp:65,96`).
  - [INV] Hard-coded AI lists in agent code: `kAiDomains` (network_win.cpp), `classify_domain`, `flow_matches_ext`, `is_browser_proc` and `detect_ai_tab` (agent.cpp), plus the `copilot-runtime`→`api.githubcopilot.com` / `claude`→`api.anthropic.com` fallback. They duplicate data/blocklists. `kAiDomains` and `classify_domain` were fixed in db50a52 (now data-driven via `set_ai_domains` / `rules.match_ai_domain`). `flow_matches_ext`, `is_browser_proc`, `detect_ai_tab` and the copilot/claude fallback are still present at HEAD (agent.cpp:435, 608, 783; network_win.cpp:403; linux/network_monitor.cpp:74).
  - [INV] `detect_ai_tab` uses loose substring matching ("claude", "grok", "mistral", "perplexity", "lovable") and raises a high-severity alert on match, so any title containing those words (a news article, a person named Claude) creates an alert. Still present at HEAD.
  - [HACK] Telemetry exclusion in `is_ai_prompt_domain` is a hard-coded substring list. The keep-alive filter thresholds (350 / 600 bytes) are magic numbers. Not re-checked individually at HEAD (telemetry list moved to `is_ai_telemetry_domain`).
  - Protocol: new `window_focus.ai_site` field and new rule-hit context fields, with no docs/PROTOCOL.md update in this commit.
  - Adds `live_ai_meter.obj` / `test_dns.obj` to .gitignore, which suggests stray local build artifacts.
- **Undid / broke:** Removes the earlier storage-activity detection (`recently_modified` in extensions_win.cpp) and the hard-coded `ide_is_running` from agent.cpp (known batch-1 issue). Commit 12 puts `ide_is_running` back.

### 12. `3f713d2` feat: Enhance IDE detection in extension scanning and update test cases for accurate reporting
- **Intent:** Attribute an AI network flow to the right IDE (VS Code vs JetBrains etc.) by walking the process parent chain, so a VS Code Copilot flow does not mark the JetBrains plugin active.
- **Touched:** agent/core/src/agent.cpp (`scan_extensions`), agent/tests/test_agent.cpp.
- **Problems:**
  - [INV] Puts back the hard-coded `ide_is_running` lambda (known from batch 1) and adds a hard-coded `is_jetbrains_exe` list and IDE path substrings (`.vscode\`, `microsoft vs code`, `jetbrains`, `github-copilot-intellij`). This worsens the known issue. Still present at HEAD (agent.cpp:525-533).
  - [HACK] Test-only branch in production code: `if (procs.empty()) return true;  // allow unit tests that omit process list`. On a real PC, an empty process snapshot makes every IDE count as "running". Still present at HEAD (agent.cpp:533).
  - `flow_belongs_to_ide` returns `true` when the chain matches no known IDE, so an unattributed flow can still mark any installed extension active. Still present at HEAD (unverified in detail).
- **Undid / broke:** Brings back the `ide_is_running` logic that f599f4d had removed.

### 13. `f7b0d77` Implement network isolation feature for AI process management
- **Intent:** Make "Isolate" enforce on the PC itself (nftables on Linux, a Windows Firewall rule via netsh) while keeping LAN + CCC reachable. Re-apply it after restarts or tampering. Also auto-kill high/critical hits in test mode and while isolated, and add many AI domains and process signatures.
- **Touched:** agent/core (isolation.hpp/.cpp, agent.cpp, policy.cpp, interfaces.hpp), agent/platform/linux (network_isolator.cpp, factory), agent/platform/windows (isolator_win.cpp, network_win.cpp, factory), data/signatures/processes.yaml, data/blocklists, installers/windows/xephyguard.nsi, server EventIngestor, tests. Touches 5 components at once.
- **Problems:**
  - [SEC][INV] The Linux isolator runs as root with shell string building: `popen(cmd + " >/dev/null 2>&1")` for `nft`. The arguments are constants and the ruleset goes in on stdin built from parsed IPv4 ints, so injection risk is low today, but this breaks the "no shell string building" rule. Still present at HEAD (`agent/platform/linux/network_isolator.cpp:24-25`).
  - [INV] Default actions for `Mode::test` change to include `kill` for every high/critical hit (`policy.cpp:95-96`). While a PC is isolated, the agent force-adds `warn` + `kill` for any high-severity hit in any mode. Only the Exam profile used to kill, and only local-LLM runtimes. This is a large enforcement change with no FEATURES/DECISIONS entry. Still present at HEAD (policy.cpp:95-96, agent.cpp:171).
  - [SEC] Broad exe names in new signatures, combined with auto-kill in test mode, can close unrelated programs. Examples: `aide` (also the Linux AIDE intrusion-detection tool, running as root), `comet`, `poe`, `monica`, `sider`, `copilot`, `qwen`, `kimi`, `grok`. Still present at HEAD (`data/signatures/processes.yaml:96,116,122+`).
  - [INV] Architecture change without a docs/DECISIONS.md entry. DECISIONS #9 puts isolation at the gateway (Unbound "isolated" view). This commit adds a second enforcement point on the PC. `isolate_notice` semantics change, and docs/PROTOCOL.md is not updated.
  - [HACK] Isolation state is reported by sending a fake `net_adapter` event (`name: "Internet"`, `kind: "net.isolated"`) instead of its own event type. Not re-checked at HEAD.
  - [SEC] Windows `isolate()` also runs `netsh advfirewall set allprofiles state on`, a system-wide change that `restore()` never undoes. `run_netsh` waits up to 30 s per call while holding a mutex. Still present at HEAD (`isolator_win.cpp:63`).
  - `apply_isolation` sets `status_.isolated = on` even when the firewall/nft call fails, so status can claim isolation that is not in force (an error is logged). Linux `restore()` always returns true. Not re-checked at HEAD.
  - [INV] The `kAiDomains` list in network_win.cpp grows by ~80 hard-coded domains that duplicate data/blocklists. Fixed in db50a52. `classify_domain` gains more hard-coded entries; fixed in db50a52.
- **Undid / broke:** Practice/always-on default high/critical actions change from `{log}` to `{log, alert}` (more alerts outside sessions).

### 14. `31453be` feat: Implement exam mode functionality to disable Task Manager on Windows
- **Intent:** Disable Task Manager (HKLM and HKCU `DisableTaskMgr`) while in test mode. Treat helper disconnects as tampering. Make the Windows service restart faster.
- **Touched:** agent/apps/helper_main.cpp, agent/core (agent.cpp, interfaces.hpp), agent/platform/windows/system_win.cpp, installers/windows/xephyguard.nsi, server EventIngestor.
- **Problems:**
  - [INV] Every helper IPC disconnect emits a `tamper` event `helper_terminated`, including normal logoff, shutdown, reboot and helper restarts. The server text says "Session helper was terminated by user", which states a cause the agent never established. That is a false, untraceable tamper flag against the student. Still present at HEAD (`agent/core/src/agent.cpp:1586`, `server/app/Services/EventIngestor.php:46`).
  - [HACK] `DisableTaskMgr` is applied for every `Mode::test` profile ("Test: AI blocked" as well as "Exam"), and in HKLM it also locks out administrators. It is only cleared by a later policy change, a clean shutdown or uninstall, so a crash leaves Task Manager disabled. The HKCU copy relies on a C++ destructor (`TaskMgrGuard`), which does not run on a crash or kill. Still present at HEAD (agent.cpp:99, system_win.cpp:112, helper_main.cpp:28).
  - `check_helpers` interval drops from 15 s to 2 s (extra load every 2 s). Still present at HEAD (agent.cpp:1552).
  - Not in docs/FEATURES.md or THREAT_MODEL.md (unverified that FEATURES lists it at HEAD).

### 15. `9b24bdf` feat: Update timezone handling and format timestamps across controllers and views
- **Intent:** Show times in lab local time instead of UTC.
- **Touched:** server (config/app.php, .env.example, EventIngestor, Internal Eval/NetEvents controllers, DashboardController, Alert model, app.js, layouts/app.blade.php, live.blade.php).
- **Problems:**
  - [HACK] `Asia/Dhaka` is hard-coded as the default `app.timezone` and twice in JS (`fmtTime` in app.js, and the same function pasted inline in layouts/app.blade.php). Fixed later: at HEAD the timezone is a setting (`admin/settings.blade.php`) and no hard-coded JS zone remains.
  - Changing `app.timezone` from UTC on an existing DB shifts how stored datetimes are read (Laravel stores them in app timezone), so earlier rows display 6 h off. There is no migration or note. Unverified whether anyone deployed before this.
  - `Alert::broadcastPayload` now sends `occurred_at` as `H:i:s`, with no date. Consumers lose the date and cannot sort across midnight. Still present at HEAD (`server/app/Models/Alert.php:25`).
  - Several JSON responses now return pre-formatted `H:i:s` strings instead of timestamps (API shape change, no tests updated).

### 16. `37c96ae` bug fixes: multiple pc addition bug fixes.
- **Intent:** Tell cloned lab PCs apart with a per-install `machine_uid`, stop merging same-hostname PCs, stop broadcasting commands to same-hostname PCs, and add Windows scripts to run the CCC natively and push agents to PCs.
- **Touched:** agent/core/src/enroll.cpp, server (AgentCommand, EnrollController, EnrollmentService, Enrollment/Pc models, PublicController, migration, EnrollmentTest), new root scripts `deploy-agents.ps1` and `run-native.ps1`. Crosses agent, server and deploy in one commit.
- **Problems:**
  - Confirms the known issue: the AgentCommand broadcast to every same-hostname PC is removed here. Gone at HEAD (`server/app/Events/AgentCommand.php` has only `pc.<id>`).
  - [INV] New enrollment protocol field `machine_uid`, with no docs/PROTOCOL.md update. Still undocumented at HEAD (`git grep machine_uid HEAD -- docs` finds nothing).
  - [SEC] `machine_uid` comes from the client and is unauthenticated. An enrollment that claims an existing PC's uid is matched to that PC row on approval (`EnrollmentService::approve`). Hostname/MAC were spoofable before as well, so this is not much worse. Still present at HEAD (unverified in detail).
  - [SEC] deploy-agents.ps1 sets WinRM `TrustedHosts` to `*` on the teacher PC (turns off WinRM server authentication for every host) and then sends admin credentials. Fixed later: HEAD `new_setup_scripts/windows/deploy-agents.ps1:172` only adds the target hosts, but leaves an existing `*` in place.
  - [SEC] deploy-agents.ps1 downloads the agent installer over plain `http://<teacher>/downloads/...` and runs it as admin on each student PC with no hash or signature check. A LAN MITM gets SYSTEM on every PC. Still present at HEAD (`new_setup_scripts/windows/deploy-agents.ps1:142,158`).
  - [HACK][SEC] run-native.ps1 runs a production-style CCC from `.env.example` (APP_ENV=local, APP_DEBUG=true, SQLite), uses `artisan serve`, builds nginx config from `dev-native.conf.example`, and patches files with regex. Debug mode leaks stack traces. Gone at HEAD (file removed; the Linux setup now writes APP_DEBUG=false).
  - [HACK] A hard-coded fallback enrollment key `'XG-A77CFB-6554C8-489B53'` in `PublicController::downloadsIndex` is in context here (it was added in 04a0d3b). Fixed in db50a52.
  - The commit name "bug fixes" hides a new identity scheme, a migration and two large deploy scripts. It mixes components.
- **Undid / broke:** Removes the `http://...:8000` server URL from the silent-install command (good, it was plaintext).

### 17. `00d697f` feat: copy/paste active file detection, browser AI overview suppression, and dynamic host resolution
- **Intent:** Add the active file name (parsed from the window title) to copy/paste source/target, suppress Google AI Overviews by forcing `&udm=14` search via browser enterprise policies in test mode, and derive the CCC address from the request Host.
- **Touched:** agent/apps/helper_main.cpp, agent/core (agent.cpp, policy.hpp/.cpp, interfaces.hpp), agent/platform/windows (helper_win.cpp, system_win.cpp), installers/windows/xephyguard.nsi, server (DashboardController, EnrollmentController, PublicController, Bootstrap, EventIngestor, PolicyBuilder, AgentApiTest).
- **Problems:**
  - [PRIV][INV] Bootstrap and PolicyBuilder now turn on `clipboard` and `keys` collection by default in every profile, Always-on (no session) included: `array_merge(['window' => true, 'clipboard' => true, 'keys' => true], ...)`. docs/ETHICS.md says typing rhythm and copy/paste metrics are not collected in Always-on. Still present at HEAD (`PolicyBuilder.php:59`, `Bootstrap.php:17`).
  - [PRIV] `extract_active_file` in the helper parses the raw window title and adds the file name to copy/paste `src`/`dst` (e.g. `code (thesis.docx)`). The agent passes it through `norm_target` without `pol.filter_title`, so file names leak even when title capture is off/app_only. They are stored in `paste_events` and shown in `last_event`. Still present at HEAD (`agent/apps/helper_main.cpp:162,389`, `agent/core/src/agent.cpp:827-836`).
  - [INV] When the clipboard probe fails, the helper sends a paste event built from the last copy's size, line count and code flag. That is evidence the system did not measure, and it can feed `behavior.large_paste` alerts. Partly fixed later (HEAD only sends it when the stored copy is non-empty), but a guessed paste is still reported (helper_main.cpp:361-375).
  - [HACK] `HelperState::mu` changed to `std::recursive_mutex`, which looks like a deadlock workaround. A detached thread captures `report_paste` (which captures helper_main locals by reference) and can outlive `helper_main` on shutdown. Still present at HEAD (helper_main.cpp:229).
  - [HACK] Disabling AI-overview suppression deletes the whole HKLM `Policies\Mozilla\Firefox\SearchEngines` tree and the Chromium `DefaultSearchProvider*` values. It also runs on every policy apply with `false` and at shutdown, so it wipes any search policy the university set by GPO. Still present at HEAD (`system_win.cpp:310`).
  - [SEC] The CCC address in the Linux install script, the /enroll page and /downloads now comes from the client-supplied `Host` header when it is an IP. Host-header spoofing is low impact without caching, but the address ends up inside a `curl | sudo bash` script. Not re-checked at HEAD.
  - [HACK] NSIS default CCC IP changes from a dev LAN address `192.168.0.51` to `192.168.0.50`, next to a hard-coded CA fingerprint. Fixed later: HEAD uses `${DEFAULT_CCC}` / `${DEFAULT_CAFP}` defines.
  - [INV] New policy field `suppress_ai_overview`, with no docs/PROTOCOL.md update. Not in docs at HEAD.

### 18. `2af82fc` WP0: core library green
- **Intent:** Start a new "xephyguard-screen" subsystem: a tile-based live screen-streaming library (capture interface, xxHash tile diffing, WebP/JPEG encoder, flow control, binary wire protocol) with tests.
- **Touched:** new top-level `xephyguard-screen/` (CMakeLists, CONTEXT.md, capture/, core/, tests/, third_party/doctest.h, xxhash.c/h).
- **Problems:**
  - [PRIV][INV] This is a system for capturing and sending student screen content. The project invariant and docs/ETHICS.md:24 say "Never collected: … screenshots or screen content". No ETHICS, DECISIONS, THREAT_MODEL or FEATURES change was made, at the time or at HEAD (git grep of docs finds no screen-view entry). The subsystem is still present and in active development at HEAD (HEAD commit is "X11 live screen fixes").
  - [INV] New dependencies without approval: libwebp 1.4.0 and libjpeg-turbo 3.0.3 via CMake `FetchContent` (network fetch at build time), plus vendored doctest and xxHash. Still present at HEAD.
  - [HACK] CMakeLists rewrites libjpeg-turbo's own CMakeLists.txt at configure time (`string(REPLACE ... "if(FALSE)")`) to force subdirectory builds.
  - New component outside the CLAUDE.md component table. It uses C++17 while agent/ is C++20, and its own CONTEXT.md is in a different doc style. Status is reported from a MinGW build only.
  - The protocol decoder checks bounds well (header size, tile count, payload limits). No problem found there.

### 19. `cca8468` WP1: in-process pipeline green (Milestone M1)
- **Intent:** End-to-end in-process test (fake capture → tiles → encoder → wire → viewer) with keyframe recovery, and golden fixtures for the browser viewer.
- **Touched:** xephyguard-screen/tests/test_pipeline.cpp, binary fixtures (`keyframe.bin`, `delta.bin`, `full.bin`), CMakeLists, CONTEXT.md.
- **Problems:**
  - [HACK] The "Dump fixtures" test case writes the golden fixtures back into the source tree (`xephyguard-screen/tests/fixtures/` and fallbacks) on every test run. So the fixtures are not really golden, and an encoder regression silently changes what the JS viewer tests compare against. Not re-checked at HEAD.
  - Inherits the screen-content privacy problem from commit 18.

### 20. `14b0a11` WP2: gateway relay and browser viewer green
- **Intent:** A Go WebSocket relay (`/agent`, `/view`) that pairs agents and viewers by stream_id, with JWT viewer auth, plus a browser canvas viewer.
- **Touched:** xephyguard-screen/gateway (auth.go, auth_iface.go, main.go, relay.go, tests, go.mod/go.sum), viewer/ (tilecanvas.js, viewer.js, tests), package.json.
- **Problems:**
  - [SEC][INV] `main.go` serves plain HTTP (`ListenAndServe` on `:8082`, no TLS, no mTLS). As committed, it uses `StaticAuthenticator`, which accepts every viewer (stream 1) and every agent ("dev-agent"). Anyone on the LAN could watch a student's screen or inject frames. The StaticAuthenticator was fixed later (HEAD requires `SCREEN_GATEWAY_SECRET` unless `-dev-insecure`). Plaintext HTTP is still present at HEAD (`gateway/main.go:118`).
  - [SEC] `AuthAgent` accepts the `X-Agent-Cert-Fingerprint` request header as proof of identity when no client cert is present ("Fallback for dev / proxy header"). Any client can set it. At HEAD `GatewayAuthenticator.AuthAgent` calls this validator first, so the header alone still authenticates an agent. Still present at HEAD (`gateway/auth.go:130`, `main.go:74-76`).
  - [SEC] Both `websocket.Accept` calls use `InsecureSkipVerify: true`, which turns off the Origin check (cross-site WebSocket hijacking). Still present at HEAD (`relay.go:179,369`).
  - [SEC] An authenticated agent picks its stream from `?stream_id=` or from each frame header (`frameStreamID`). Nothing binds agent identity to a stream, so any agent can publish into or take over another student's stream. Still present at HEAD (`relay.go:170-175,259-272`).
  - [SEC] The JWT is accepted from the query string (`?token=`, which leaks into logs and history). `exp` is not required: jwt/v5 checks it only if present, so a token without `exp` stays valid after the 5-minute jti memory. CONTEXT.md claims "Validates exp" and "Agent identification via … mTLS" and does not mention the header fallback. Still present at HEAD (auth.go:71; no `WithExpirationRequired`).
  - [INV] New dependencies: Go modules `github.com/coder/websocket`, `github.com/golang-jwt/jwt/v5`, and an npm `package.json`. go.mod marks both direct deps `// indirect`. Not approved, not in DECISIONS.
  - Inherits the screen-content privacy problem from commit 18.

### Batch 2 summary
1. [PRIV][INV] Commits 18–20 add a live screen-streaming subsystem (`xephyguard-screen/`), which breaks the "never collect screen content" invariant. No ETHICS/DECISIONS/THREAT_MODEL change was made. Still present and growing at HEAD.
2. [SEC] The screen gateway (20) runs plain HTTP with no mTLS, trusts a spoofable `X-Agent-Cert-Fingerprint` header, turns off the WebSocket Origin check, and lets any agent publish into any stream_id. The default no-auth StaticAuthenticator was fixed later; the other four are still present at HEAD.
3. [PRIV][INV] Commits 11 and 17 force full window titles, window/app tracking, clipboard metrics and keystroke timing on in every profile, Always-on included, against docs/ETHICS.md. The raw title also bypasses `filter_title` in `web.ai_tab` hits and in copy/paste file names. Still present at HEAD.
4. [PRIV][SEC] Commit 11 adds `SIO_RCVALL` raw packet capture as SYSTEM and silent HKLM browser-DNS policy changes on Windows, with no ETHICS or threat-model entry. Still present at HEAD.
5. [INV][SEC] Commit 13 makes kill the default for every high/critical hit in test mode and while isolated. Combined with broad exe signatures (`aide`, `comet`, `poe`, `copilot`), it can close unrelated programs. It also adds a root `popen` shell call for nft. Still present at HEAD.
6. [INV] Commit 14 raises a `helper_terminated` tamper flag ("terminated by user") on every helper disconnect, including logoff and reboot, and leaves Task Manager disabled machine-wide if the agent crashes. Still present at HEAD.

---

## Batch 3 — commits 21–30 (2026-11-11 → 2026-11-11)

All ten commits landed on the same day. None of them touch docs/ETHICS.md, docs/DECISIONS.md, docs/PROTOCOL.md or docs/THREAT_MODEL.md (`git log 2af82fc^..3e51736 -- docs` is empty). At HEAD, docs/ETHICS.md:24 still says "**Never collected:** ... screenshots or screen content", and none of those four docs mention screen streaming at all.

### 21. `4586163` WP3: agent state machine, Linux X11 and Windows GDI/DXGI capture green
- **Intent:** Add the screen agent state machine (Idle/Armed/Streaming/Paused*) with a threaded capture loop, plus capture backends for X11 (XShm), Windows GDI BitBlt and DXGI Desktop Duplication.
- **Touched:** xephyguard-screen/agent/agent_state.{h,cpp}, capture/x11_capture.cpp, capture/win_gdi_capture.*, capture/win_dxgi_capture.*, tests/test_agent_state.cpp, CMakeLists.txt, CONTEXT.md.
- **Problems:**
  - [SEC][PRIV] x11_capture.cpp creates its shared-memory segment with `shmget(IPC_PRIVATE, ..., IPC_CREAT | 0777)`. The segment holds the raw screen pixels, and with 0777 any local user can attach to it and read the screen, or write into it. It should be 0600. The segment is also only marked IPC_RMID in cleanup(), so it leaks if the process crashes. Still present at HEAD (x11_capture.cpp:43-44).
  - [HACK] CONTEXT.md claims "XDamage dirty rectangle tracking", but the code only creates the XDamage object and never reads its events. Every capture copies the full frame and never returns NoChange. Still present at HEAD: the damage object is still created and unused, and line 135 now admits that full-frame capture has no dirty list.
  - [SEC] At this commit the X11 capture assumed 32 bpp and did not check `bits_per_pixel`, so a 16/24 bpp display would give the encoder a stride smaller than width*4. Fixed in 4ac2d8e, which added bpp handling.
  - [bug] Snapshot mode ends the capture thread with `running_=false; break;` without joining it. The next `on_view_start()` sees `running_==false` and assigns a new `std::thread` to the still-joinable `capture_thread_`, which calls `std::terminate`. CONTEXT.md says "thread joined", which is wrong. The test only runs one snapshot, so it misses this. Still present at HEAD (agent_state.cpp:88-91 and 355-359).
  - [bug] capture_loop() calls `sleep_for` (50/100 ms) while holding `mutex_`, and it calls `sender_` (network I/O) under the lock for MASKED/SECURE placeholders. This stalls ACK handling and control calls. SECURE placeholders are re-sent every 100 ms. Still present at HEAD.
  - [INV] The new top-level `xephyguard-screen/` component is built as C++17 (`CMAKE_CXX_STANDARD 17`), while the project stack is C++20. It is not one of the components listed in CLAUDE.md, and no DECISIONS.md entry covers it. Still present at HEAD.
  - [HACK] "Linux X11 ... green" is overstated. The tests use FakeCapture only. CONTEXT.md says the build was MinGW, where x11_capture.cpp is not compiled. No X11 libraries are linked, and no header or factory exposes X11Capture. The Linux backend was never compiled or run in this commit (unverified that it even compiled).

### 22. `6d6b122` WP4: privacy, modes, capabilities green
- **Intent:** Add a PrivacyFilter (mask the stream when the foreground window matches process, class or title-regex rules, with fail-safe masking on errors), wall mode (320 px downscaled keyframes every 5 s), snapshot quality, cursor messages, Wayland capability detection and a screen indicator.
- **Touched:** agent/privacy.*, agent/indicator.*, agent/capabilities.*, core/downscale.*, agent_state.*, three new test files, CONTEXT.md.
- **Problems:**
  - [PRIV] `PrivacyFilter::set_exam_mode(true)` turns off all privacy masking, so password managers, banking or messaging windows would stream unmasked during exams. No ETHICS.md text allows this. Still present at HEAD (privacy.cpp).
  - [PRIV][HACK] `ScreenIndicator` is only a bool with set_visible() and is_visible(), and it draws nothing. CONTEXT.md calls it "persistent indicator management". At HEAD it is still a stub, and no non-test code in xephyguard-screen uses it.
  - [PRIV] PrivacyFilter is never wired into the real daemon. At HEAD, nothing outside tests calls `set_privacy_filter`, `set_window_provider` or `set_rules`, so the standalone xgagent streams with no privacy masking at all. Still present at HEAD.
  - [PRIV] capabilities.cpp reports `silent_capture=true` on Windows and X11. It advertises capture with no user prompt, which conflicts with ETHICS.md's transparency rule. Still present at HEAD.
  - [bug] `const std::string& mode() const` returns a reference to `mode_` after the lock is released, which is a data race. The regex is rebuilt for every rule on every loop pass, and title regexes from config are unbounded, so there is a ReDoS risk if rules ever come from the CCC. Still present at HEAD.
  - [HACK] Cursor messages put the x/y position into the `width`/`height` header fields, and the cursor shape is sent as a fake Raw tile. This protocol change is not documented in docs/PROTOCOL.md.
  - [HACK] `downscale_box` trusts `src.bgra.size() >= stride*height` without checking it. The input is local capture data, so the risk is low.

### 23. `5ea458a` WP5: CCC API and web viewer UI
- **Intent:** Laravel endpoints that issue, renew and stop 30 s HS256 JWTs for viewing a student's screen; a screen_view_audits table; a dashboard "screen" tab; a browser viewer (viewer.js, tilecanvas.js); and an AgentHealthClassifier.
- **Touched:** server ViewTokenController, ScreenViewAudit model and migration, AuthController (end audits on logout), DashboardController, config/services.php, routes/api.php, resources/js/app.js, js/screen/*, live.blade.php, composer.json/lock, tests, xephyguard-screen/CONTEXT.md.
- **Problems:**
  - [SEC][HACK] config/services.php fell back to `SCREEN_GATEWAY_SECRET = 'dev-secret-change-in-production-12345'`. With env unset, anyone could forge viewer JWTs. Fixed in db50a52 (now derived from APP_KEY or empty).
  - [SEC][INV] The default gateway URL was `ws://127.0.0.1:8082`, a plaintext WebSocket outside the :8443 mTLS path, and the JWT travels in the `?token=` query string, where it ends up in logs. The default is now empty at HEAD (db50a52), but the token is still passed in the query string.
  - [SEC] `issueToken` accepts a client-supplied `stream_id` and signs it into the JWT. The gateway uses the JWT's stream_id to choose which stream a viewer joins, so an instructor who owns any active session can request a token for another student's or PC's stream. Stream ids are guessable (`student_id*1000 + session_id`, which also collides, e.g. student 1/session 1000 vs student 2/session 0). Still present at HEAD (ViewTokenController.php:65-69).
  - [INV] New dependency `firebase/php-jwt` with an unpinned `"*"` constraint, added without the approval CLAUDE.md requires. Still present at HEAD (composer.json:11).
  - [bug] app.js called `new ScreenViewer(wsUrl, canvas); viewer.connect()`, but the constructor is `(canvas, options)` and `connect(url)`, so the screen tab was broken when it shipped. Fixed in 3e51736.
  - [bug] The renew timer calls `/renew` every 20 s and throws away the new token, so a reconnect would use the expired original. Not checked at HEAD (the dashboard JS was rewritten later).
  - [INV] `AgentHealthClassifier` labels PCs `SUSPECTED_TAMPERING` and returns only a string, with no traceable reasons. It guesses among four field names for sleep/shutdown. It is unrelated to the screen viewer, so this commit mixes concerns. It is unused at HEAD: only its own file references it.
  - [INV] One commit spans server/ and xephyguard-screen/ and adds an agent↔CCC streaming path with no PROTOCOL.md, THREAT_MODEL.md, ETHICS.md or DECISIONS.md update.
  - Positive: screen_view_audits stores only metadata (who, whom, mode, start/end). No frames are stored server-side in this commit.

### 24. `cf3d23c` WP6: agent main daemon and load benchmark green
- **Intent:** Add the `xgagent` executable (picks the DXGI, GDI or X11 backend, starts streaming) and the `xgbench` load benchmark.
- **Touched:** agent/agent_main.cpp, bench/load_agents.cpp, CMakeLists.txt.
- **Problems:**
  - [bug] agent_main.cpp includes `../capture/x11_capture.h`, which did not exist until db50a52, and the build links no X11 libraries. The Linux build of xgagent was broken from this commit until db50a52. "green" here means MinGW only.
  - [HACK] The frame sender is a no-op (`(void)data;`), and the daemon starts capturing the whole screen at launch with no view_start from the CCC.
  - [HACK] The "load benchmark" runs in-process threads that use FakeCapture, the encoder and simulated RTT and packet loss. It never touches the gateway, the network or AgentStateMachine, so it says nothing about real CCC load.

### 25. `15d6d00` docs: add standalone HTML test page for browser viewer
- **Intent:** A standalone viewer test page.
- **Touched:** xephyguard-screen/viewer/index.html.
- **Problems:**
  - [HACK] Hard-codes `ws://127.0.0.1:8082/view?token=dev`, a plaintext socket with a dummy token. It works only because the gateway's `StaticAuthenticator` (main.go) accepted every viewer and agent at this point. Fixed at HEAD: gateway auth is now JWT or agent-secret, and viewers without a token are only accepted in a `devInsecure` mode. The page itself is unchanged.
  - The subject says "docs", but the commit adds a code and test page.

### 26. `22b42fb` feat: add WinHttp WebSocket client to xgagent for live gateway streaming
- **Intent:** Make xgagent actually send frames to the gateway over a WinHTTP WebSocket and receive ACK/KeyframeReq messages back.
- **Touched:** agent/agent_main.cpp, CMakeLists.txt (links winhttp).
- **Problems:**
  - [SEC][INV] The WebSocket is plaintext. `WinHttpOpenRequest` has no `WINHTTP_FLAG_SECURE`, there is no client certificate and no auth header, and the agent connects to `ws://host:8082/agent?stream_id=N`. This is a non-mTLS agent→CCC path carrying screen content, which breaks the :8443 mTLS invariant. Still present at HEAD: it still logs `ws://` (agent_main.cpp:378, 404) and now adds `&agent_token=` to the URL over the same plaintext socket.
  - [PRIV] The screen is captured and streamed from process start, whether or not anyone is viewing. No privacy filter and no indicator are wired in.
  - [bug] `receive()` uses a fixed 4096-byte buffer and ignores `WINHTTP_WEB_SOCKET_*_FRAGMENT_BUFFER_TYPE`, so larger messages are split into bogus messages. There is a thread-safety bug: `ws_` (a plain HINTERNET) is used by the rx thread while the main thread calls `close()` before joining rx_thread, which is a use-after-close race. The 072b8d8 reconnect logic made this worse.
  - [bug] `std::stoul` / `std::stoi` on `--stream` / `--gateway` have no try/catch, so bad arguments crash the agent.

### 27. `b1006f2` build: statically link libgcc and libstdc++ for fully portable xgagent.exe
- **Intent:** Static MinGW link so xgagent.exe runs standalone.
- **Touched:** xephyguard-screen/CMakeLists.txt.
- **Problems:** none found. It exists to support handing out a standalone .exe; see 28 and 29.

### 28. `4671838` ui: clean up duplicate downloads and add descriptive cards for setup vs screen agent
- **Intent:** Show labelled cards on the public /downloads page for the setup installer and xgagent.exe.
- **Touched:** server/app/Http/Controllers/Web/PublicController.php.
- **Problems:**
  - [SEC][PRIV] Adds to the known public /downloads issue (04a0d3b). The unauthenticated page now advertises a portable screen-capture executable. The diff removes no duplicates; the "clean up" presumably happened in untracked storage (unverified).
  - [PRIV] The public description says the service collects "keystrokes". The agent only collects keystroke counts, so the wording misstates what is collected. Not checked at HEAD.
  - [HACK] Hard-coded filename `XephyGuard-Setup-0.1.0.exe` and inline-HTML string building in the controller.

### 29. `072b8d8` feat(screen): add ad-hoc PC live streaming and agent auto-reconnect
- **Intent:** Let staff view a PC's screen with no active session (`/api/screen/pc/{pc}/token`, stream_id = pc_id), add auto-reconnect and agent.toml autodetect to xgagent, and allow `.bat` launchers on /downloads.
- **Touched:** ViewTokenController (issuePcToken), routes/api.php, ScreenViewAudit and a migration (student_id nullable, pc_id), app.js, PublicController, agent_main.cpp, ViewTokenTest.
- **Problems:**
  - [SEC][PRIV] `issuePcToken` checks ownership only when the PC is in an active session. With no session, any authenticated user of any role can view any PC's live screen. This is the most serious privacy widening in the batch: viewing outside a supervised session with no role gate. Still present at HEAD (ViewTokenController.php:116-147). It also accepts a client-chosen `stream_id`, as in 23.
  - [HACK][INV] A hard-coded default CCC IP `192.168.0.51` in the interactive prompt (config must come from env or the wizard). Gone at HEAD.
  - [SEC] xgagent reads `agent.toml` from the current directory and `../` as well as ProgramData. A file planted in the working directory can redirect the screen stream to an arbitrary host. Still present at HEAD (agent_main.cpp:156-157).
  - [bug] `reconnect()` runs inside the frame-sender callback, on the capture thread, so it blocks capture during connect. `send_binary` failure calls `close()` while the rx thread may be inside `WinHttpWebSocketReceive` on the same handle. This widens the race from 26. Not checked whether HEAD fixed it.
  - [bug] Stream-id scheme mismatch. The agent uses pc_id (from agent.toml), but when a student is on the PC, app.js still calls the student endpoint, which issues `student_id*1000+session_id`. Viewer and agent then join different streams. HEAD status unverified (dashboard JS rewritten).
  - [INV] Cross-component change (server/ and xephyguard-screen/) with no PROTOCOL.md update. The public /downloads page now also serves `.bat` launchers.

### 30. `3e51736` fix(viewer): pass canvas correctly to ScreenViewer and support flexible constructor args
- **Intent:** Fix the WP5 constructor misuse and accept both `(canvas, opts)` and `(url, canvas, opts)`.
- **Touched:** server/resources/js/app.js, server/resources/js/screen/viewer.js, xephyguard-screen/viewer/viewer.js.
- **Problems:**
  - [HACK] It fixes the caller but also adds an overloaded constructor that uses `arguments[2]`, to tolerate the wrong call order. `tileCanvas` can now be `null`, but `handleBinaryFrame` calls `this.tileCanvas.setBanner/setSize/drawTiles` without a null check, which throws a TypeError on every frame. Still present at HEAD (viewer.js:199-208).
  - [HACK] viewer.js is copy-pasted in two places (identical at this commit). The copies have diverged at HEAD (different hashes).
- **Undid / broke:** fixes the broken screen-tab viewer call introduced in 5ea458a.

### Batch 3 summary
1. [PRIV][INV] Live screen streaming sends screen content although docs/ETHICS.md:24 says screen content is never collected. No ETHICS, DECISIONS, PROTOCOL or THREAT_MODEL update in the batch. PrivacyFilter is never wired into xgagent, ScreenIndicator is a stub, and exam mode disables masking. Still present at HEAD.
2. [SEC][PRIV] `issuePcToken` (072b8d8) lets any logged-in user view any PC that is not in an active session. Both token endpoints also sign a client-chosen `stream_id`. Still present at HEAD.
3. [SEC][INV] Agent→gateway is plaintext `ws://` on :8082 with no mTLS, outside the :8443 path. At HEAD it also puts the agent token in the URL. Still present at HEAD.
4. [SEC] The X11 shared-memory screen buffer is created with mode 0777, readable by any local user. Still present at HEAD.
5. [SEC][HACK] Hard-coded fallback JWT secret `dev-secret-change-in-production-12345` (5ea458a). Fixed in db50a52.
6. [bug] A second view_start after a snapshot hits `std::terminate`, because the snapshot thread is never joined. The WinHTTP client also has close/receive races. The snapshot bug is still present at HEAD.

---

## Batch 4 — commits 31–40 (2026-11-12 → 2026-11-13)

### 31. `e1be056` fix(screen): add keyframe caching, auto-stream routing, and viewer keyframe recovery
- **Intent:** Show a picture to new viewers right away by caching the last keyframe in the gateway, re-request keyframes from the viewer, and send a viewer to "whatever stream is live" when its own stream has no agent.
- **Touched:** xephyguard-screen/gateway/main.go, relay.go, relay_test.go; viewer.js (both the standalone and the server copy).
- **Problems:**
  - [SEC] The new `FlexibleAuthenticator` (main.go) makes viewer auth optional. If the JWT check fails, it logs the failure and then accepts `?stream_id=N` with no auth. With no token at all it returns stream 1. `AuthAgent` always returns `"agent", nil`, so any host can connect as an agent. In practice anyone on the LAN could watch any PC's screen or push fake frames. Fixed in db50a52: `GatewayAuthenticator` requires a JWT unless `-dev-insecure` is set.
  - [SEC] A hard-coded default JWT secret `dev-secret-change-in-production-12345` (main.go `-secret` flag default). It matches the default in server/config/services.php, so anyone who has read the repo can forge viewer tokens. Removed from source in db50a52, but it is baked into the gateway.exe committed in dbf3148, which stays in git history.
  - [SEC][PRIV] `findStreamForViewer` steps 1–2: if the requested PC has no agent, the viewer is silently routed to *any other* stream that has a live agent. An instructor asking for PC A gets PC B's screen with nothing on screen saying so. Fixed in 6d9ce20.
  - [PRIV] The gateway keeps the last full keyframe (screen content) in RAM (`stream.lastKeyframe`). It is pushed to every new viewer and, in this version, kept after viewers leave. RAM only, nothing written to disk (checked: no file writes in gateway/capture at HEAD). It was cleared on agent disconnect only from 31594cf. The cache itself is still present at HEAD (relay.go:45, 277).
  - [SEC] Already there before this commit and still there: `websocket.AcceptOptions{InsecureSkipVerify: true}` on both /agent and /view. This turns off the WebSocket Origin check. Still present at HEAD (relay.go:179, 369).
  - Minor: relay_test.go was loosened to accept an extra KEYFRAME_REQ before the ACK. That is reasonable, but it now only checks message order loosely. The viewer sends `requestKeyframe(0, 0)` on open with stream id 0.
  - [INV] Gateway serves plain HTTP/WS (`ListenAndServe`, `:8082`) with no TLS of its own. Still present at HEAD (main.go).

### 32. `5b6156d` feat(agent): embed real-time screen capture directly into xephyguard-helper
- **Intent:** Put screen capture and a WinHttp WebSocket client inside the user-level helper, so every agent PC streams to the gateway without the separate xgagent.
- **Touched:** agent/apps/screen_streamer.{cpp,hpp} (new, 276 lines), agent/apps/helper_main.cpp, agent/CMakeLists.txt (pulls in ../xephyguard-screen), xephyguard-screen/CMakeLists.txt, server PublicController download text.
- **Problems:**
  - [PRIV][INV] docs/ETHICS.md:24 says screenshots and screen content are "**Never collected**", and the spec (line 250) says screen content is never stored. This commit makes every enrolled PC send its screen to the CCC. ETHICS.md, DECISIONS.md, PROTOCOL.md and THREAT_MODEL.md were not updated, and none of them mention live screen streaming even at HEAD (only grep hit is the "never ... screen content" line in PROTOCOL.md:61). Still present at HEAD.
  - [PRIV] Streaming starts as soon as the helper starts (`check_screen_stream()` → `on_view_start(stream_id,"live")`), whether or not anyone is watching. The screen is captured and sent nonstop. HEAD now gates capture on viewer status / KEYFRAME_REQ (screen_streamer.cpp ~233–256), so this is fixed later.
  - [SEC][INV] The agent→CCC channel is plaintext and not mTLS. `WinHttpOpenRequest(..., 0)` has no `WINHTTP_FLAG_SECURE`, uses a hard-coded port `8082`, and sends no auth at all. This is a second agent↔CCC path outside the :8443 mTLS rule. At HEAD the TLS flag exists, but `tls` defaults to `false`, port 8082 is still the fallback (helper_main.cpp:529–538), and even the TLS path uses a shared token, not a client cert. Still present at HEAD.
  - [SEC] Thread safety: one `HelperWsClient` is used from the capture callback (send + `reconnect()`) and from `rx_thread` (receive), with no mutex. `close()` on a send error frees HINTERNET handles that the other thread may be using inside `WinHttpWebSocketReceive`. Still present at HEAD (no mutex in screen_streamer.cpp; both threads call `reconnect()`, lines 219 and 261).
  - Bug: if `on_view_start` fails, `run()` returns with `rx_thread` still joinable (std::terminate when it is destroyed), and that thread holds a reference to the destroyed local `state_machine`. Also, `worker_thread` is never joined before `start()` assigns a new thread, so the next `start()` calls std::terminate. Gone at HEAD (early return removed).
  - [HACK] On Linux the X11 capture runs, but the send callback is `(void)data`, so it captures for nothing. Fixed at HEAD (Linux WebSocket client added).
  - [INV] The helper reads `agent.toml` again every 10 s from the ping loop (`check_screen_stream`). Because this user-level helper has to read the root/SYSTEM config, commit 33 had to loosen ACLs.
  - [INV] One commit touches agent, xephyguard-screen and server. It also limits `xgtests` (including `test_privacy.cpp`) to standalone builds of xephyguard-screen, and the agent build uses `EXCLUDE_FROM_ALL`. So the screen tests never run under `ctest --test-dir agent/build` or in CI (CI has no Go or screen-test job either). Still present at HEAD (xephyguard-screen/CMakeLists.txt:135, ci.yml only does `cmake -S agent`).

### 33. `4fd1a79` fix(agent): propagate ccc_host and pc_id over IPC to helper and grant ProgramData read permissions
- **Intent:** Have the agent send `ccc_host`/`pc_id` to the helper over IPC, and let normal users read ProgramData\XephyGuard so the helper can load `agent.toml`.
- **Touched:** agent/apps/helper_main.cpp, agent/core/src/agent.cpp (`push_helper_config`), installers/windows/xephyguard.nsi.
- **Problems:**
  - [SEC] The NSI now grants `*S-1-5-32-545:(OI)(CI)RX` (BUILTIN\Users) on all of `%ProgramData%\XephyGuard`. That folder holds `agent.key` (the agent's mTLS private key; config.cpp:38 `cfg + "agent.key"`) and `agent.toml` (identity token, poll_token, and from db50a52 the screen agent token). On Windows the key is written with plain `write_file_atomic` and no explicit ACL (crypto.cpp, `#else` branch), so it inherits the folder's rights. Any student account can now read the PC's client key and impersonate the PC to the CCC. The comment just above still says "SYSTEM + Administrators only". Still present at HEAD (xephyguard.nsi:196).
  - [SEC] Unenrolled PCs (empty `pc_id`) default to `pc_id = 1`, so they stream into PC #1's stream. Fixed in acd236a.
  - [HACK] The installer's default CCC IP was changed from `192.168.0.50` to `192.168.0.51`, a developer's own LAN IP. The hard-coded CA fingerprint is still there. Fixed in db50a52 (now `${DEFAULT_CCC}`/`${DEFAULT_CAFP}`), but both values remain in history.
  - [SEC] IPC `config` values (`ccc_host`, `pc_id`) are used directly to open an outbound connection. The `stoul` result is cast to uint32 with no range check, so it truncates on 64-bit Linux. Low impact, and still the same shape at HEAD (helper_main.cpp:518–528).
  - Mixes agent core, helper and installer in one commit.

### 34. `6d9ce20` fix(screen): provide seamless seat-to-seat screen switching on dashboard
- **Intent:** Keep the dashboard drawer streaming when the instructor clicks another seat, and stop the gateway from sending viewers to an unrelated stream.
- **Touched:** server/resources/js/app.js, live.blade.php, screen viewer/tilecanvas JS (two copies), gateway relay.go.
- **Problems:**
  - Fixes the cross-stream routing from e1be056 (good).
  - [PRIV] A live view now starts with no explicit click. `switchTab('screen')` and `open(id)` while on the screen tab (or after watching) call `startScreen()` automatically, so just opening a PC on the Screen tab starts watching that student. An audit row is still created per view. Gone at HEAD (the drawer was replaced in abfc9df; no `switchTab`/`startScreen` left).
  - [SEC] The agent still picks its stream by `?stream_id=` and, per frame, by the stream id in the frame header. The latest connection takes over `agentConn`. The agent identity is never tied to the PC/stream id, so any authenticated agent can replace another PC's feed. Still present at HEAD (relay.go:186–196, 264–273).
  - Streams are no longer removed when the agent disconnects, so the map only grows. The cached keyframe stayed visible after disconnect until 31594cf.
  - [INV] Copy-pasted code: every viewer/tilecanvas change is made twice (xephyguard-screen/viewer/ and server/resources/js/screen/).

### 35. `acd236a` fix(agent): prevent helper stream id mismatch and blocking reconnect deadlock
- **Intent:** Stop defaulting `pc_id` to 1, push config to the helper right after enrollment is approved, and reconnect from the rx thread.
- **Touched:** agent/apps/helper_main.cpp, screen_streamer.cpp, agent/core/src/agent.cpp, gateway relay.go (logging).
- **Problems:**
  - Fixes the `pc_id = 1` fallback from 4fd1a79.
  - [SEC] The "deadlock fix" makes the thread race worse. `rx_thread` now calls `ws_client->reconnect()` (close + reopen handles) while the capture callback may also call `reconnect()`/`send_binary()`. `stop()` now calls `ws_client->close()` from a third thread while the rx thread can be blocked in `WinHttpWebSocketReceive`. Nothing is locked, so double-close or use-after-free of HINTERNET handles is possible. Still present at HEAD (no mutex; reconnect at lines 219 and 261; close in `stop()`).
  - `on_view_start` is called again from the rx thread on reconnect without checking whether capture is already running (HEAD now checks `is_thread_running()`).
  - Gateway logs every keyframe (size, stream id, remote addr). This is noisy, but only metadata, no content. Still present at HEAD (relay.go:276).
  - Port `8082` is still hard-coded in both helper call sites (made configurable in db50a52, still the fallback at HEAD).
  - Mixes agent and gateway in one commit.

### 36. `7cd93b2` feat(branding,server): integrate Xerophyte Labs identity, code signing, and server runners with screen gateway
- **Intent:** Add branding (icons, logos, version resources), a self-signed code-signing setup, and Windows run/stop scripts that also start the gateway.
- **Touched:** 35 files: branding/ (images, zip, svg, .cer), agent .rc files + CMake, installers/windows (NSI, .cer, .ico), scripts/sign_binaries.ps1, run-server.{ps1,bat}, stop-server.{ps1,bat}, server favicon/logos, app.blade.php, .gitignore.
- **Problems:**
  - [SEC] Hard-coded PFX export password in scripts/sign_binaries.ps1: `ConvertTo-SecureString -String <redacted>`. The `.pfx` itself was not committed (`*.pfx` added to .gitignore; `git log --all -- '*.pfx'` is empty). Replaced by `$env:XG_PFX_PASSWORD` in db50a52, but the password stays in history.
  - [SEC] The installer now adds a **self-signed** code-signing certificate (key marked `-KeyExportPolicy Exportable`, valid 10 years) to **LocalMachine\Root** and **LocalMachine\TrustedPublisher** on every lab PC. Whoever holds that PFX can sign code that every lab PC trusts with no prompt. Root is not needed for Authenticode publisher trust of a self-signed leaf in this way, and it widens trust. Still present at HEAD (xephyguard.nsi:206–210).
  - [INV] Certificates committed to git: `branding/XerophyteLabs-CodeSigning.cer` (906 B) and `installers/windows/XerophyteLabs-CodeSigning.cer` (906 B). They are public certs, not keys, but the rule says never commit certs. Both still tracked at HEAD.
  - [INV] Large binaries committed: `branding/xephyguard-logos.zip` (3,337,902 B), seven JPG logo concepts (~0.36–0.74 MB each), `shield_emblem_1024.png` (443,801 B), and `app.ico` (125,332 B) in four copies. All still tracked at HEAD.
  - [HACK] sign_binaries.ps1 hard-codes the developer's local paths (`d:\Project\Running\XephyGuard\...`, `XephyGuard-Setup-0.1.0.exe`). Removed in db50a52.
  - [SEC][HACK] run-server.ps1 starts the gateway as `-addr :8082` with no `-secret`, so it falls back to the public default secret from e1be056. It also runs `php artisan serve --host=0.0.0.0` (plain-HTTP dev server for the dashboard login, open to the LAN) and Reverb on 0.0.0.0, and prints `ws://<LAN-IP>:8082/`. run-server.ps1 was deleted in db50a52, but its replacement start_server/windows/start-server.ps1:104 still runs `artisan serve --host=0.0.0.0`. Still present at HEAD (now with an env secret).
  - [INV] A huge mixed commit (branding + agent + installer + server + scripts) with no docs update for the new trust anchor (THREAT_MODEL.md).

### 37. `785316a` feat(server): auto-start screen streaming gateway on port 8082 in run-native.ps1
- **Intent:** Have run-native.ps1 also start the gateway and open its firewall port.
- **Touched:** run-native.ps1.
- **Problems:**
  - [SEC][INV] Adds 8082 to the inbound Windows Firewall rule with no `-RemoteAddress`/`-Profile` limits. A plaintext WS port is now reachable from any network. The same pattern is still present at HEAD: new_setup_scripts/windows/setup-server.ps1:410–415 opens `$GatewayPort`. The gateway binds `:8082` on all interfaces by default (main.go; install.sh:140 `SCREEN_GATEWAY_ADDR=:8082`), which contradicts the compose note "Internal services bind to 127.0.0.1 only".
  - [SEC] Starts the gateway with no `-secret`, so it uses the public default secret (fixed in db50a52 when the default was removed).
  - [HACK] The gateway start block is copy-pasted from run-server.ps1 (36). Gone at HEAD (run-native.ps1 deleted in db50a52).

### 38. `dbf3148` feat(screen): track pre-compiled gateway.exe so server does not require Go installation
- **Intent:** Commit a built gateway.exe so Windows servers don't need Go.
- **Touched:** xephyguard-screen/gateway/gateway.exe (binary, **10,475,008 B**).
- **Problems:**
  - [INV] A compiled 10 MB binary committed to git, the very file .gitignore lists (`/xephyguard-screen/gateway/gateway.exe`, added in 7cd93b2), so it had to be force-added. It cannot be reviewed or rebuilt from a known source.
  - [SEC] The binary contains the hard-coded secret `dev-secret-change-in-production-12345` (`git log -S` hits dbf3148). It was built from the `FlexibleAuthenticator` source, which has the auth fallback and accepts any agent. It stays in history.
  - Still present at HEAD: `git ls-files` still lists it (replaced by a 7,208,960 B build in db50a52). It is also **stale**: the Go sources changed after its last rebuild (de47069, 31594cf, 4ac2d8e), so run-server scripts that prefer the .exe run code that doesn't match the source (e.g. without the 31594cf keyframe-clear fix).

### 39. `db50a52` feat(setup,server,agent): remove hardcoded config and organize cross-platform scripts
- **Intent:** Remove hard-coded IPs, secrets, paths and timezone; reorganize setup/start scripts into new_setup_scripts/ and start_server/; add a local Docker stack; make gateway auth real; move AI-domain lists into signatures.
- **Touched:** 84 files, +4133/−1421: agent core (config, paths, rules, signatures, agent.cpp), Windows adapters, helper/streamer, gateway main.go + gateway.exe, xephyguard-screen X11 capture, server (ViewTokenController, PublicController, PolicyBuilder, config, JS, views, tests), ccc-deploy (compose, Dockerfile, nginx, install.sh), installers (NSI, Ansible, systemd), many new scripts; deleted root run-native.ps1/run-server.ps1/deploy-agents.ps1/stop-server.ps1.
- **Problems:**
  - Did fix (checked against the diff): gateway default secret and the auth fallback (now JWT required unless `-dev-insecure`, which none of the scripts pass); PFX password moved to an env var; NSI default IP, CA fingerprint and the hard-coded enrollment key `XG-A77CFB-6554C8-489B53` removed (NSI and PublicController); Ansible CCC IP; `ccc_ip` default; `Asia/Dhaka` timezone. It also partly fixes the batch-1 hard-coded lists: the `classify_domain` domain chain in agent.cpp and the ~150-entry `kAiDomains` array in network_win.cpp were replaced with `rules_.match_ai_domain()` from signatures. The IDE process list (`is_extension_host` → `is_extension_host_proc`, agent.cpp:429) is still hard-coded at HEAD.
  - [SEC] A new screen auth model: one **shared** `SCREEN_GATEWAY_AGENT_SECRET` for every PC. PolicyBuilder sends it to every agent, the agent stores it in agent.toml (`[screen] token`), which is readable by Users after 4fd1a79, and passes it over IPC to the user-level helper (`push_helper_config` → `"agent_token"`). Any student can get it and, with the per-frame stream-id claim (see 34), push frames as any PC. It is also accepted as a URL query param (`agent_token=`), so it can end up in access logs. Still present at HEAD (PolicyBuilder.php:128, main.go AuthAgent, agent.cpp push_helper_config).
  - [SEC][INV] Still a non-mTLS agent→CCC path. Production routes agent streams through nginx `listen 443` `/screen-gw/` (no `ssl_verify_client`) or direct plaintext `:8082`. PolicyBuilder sets `tls=false` unless the port is 443/8443 or the URL is https/wss, and the default `SCREEN_GATEWAY_URL` is `ws://127.0.0.1:8082`. new_setup_scripts/README.md:63 documents a "Direct HTTP (ports 8000 / 8080 / 8082)" mode. Still present at HEAD.
  - [INV] The new `screen` policy field (port/path/tls/agent_token) and the helper IPC `screen` object are a protocol change, but docs/PROTOCOL.md was not updated. No docs/ files changed in this batch at all. Still missing at HEAD.
  - [SEC] Config::load now takes `XG_CCC_HOST`, `XG_SCREEN_TOKEN`, `XG_SCREEN_TLS` etc. from the environment, and paths.cpp takes `XG_CONFIG_DIR`/`XG_IPC_ENDPOINT`. The SYSTEM service's environment is admin-controlled, but the same code runs in the user-level helper, so a student can redirect the helper's config/IPC. Low impact; still present at HEAD.
  - [SEC] Carried over in the moved new_setup_scripts/windows/deploy-agents.ps1: the installer is downloaded over `http://$TeacherIP/downloads/...` and run as admin on each student PC with no hash or signature check, and targets are added to WinRM TrustedHosts. It is better than before (was `TrustedHosts "*"`), but MITM on the lab LAN can swap the installer. Still present at HEAD (deploy-agents.ps1:141, 154–181).
  - [INV] gateway.exe re-committed (10,475,008 → 7,208,960 B) despite .gitignore. Still tracked at HEAD.
  - Behaviour change: the `APP_TIMEZONE` default changed `Asia/Dhaka` → `UTC` (config/app.php). The timetable/scheduler runs in this zone, so an existing install without `APP_TIMEZONE` moves its schedule. The new admin "timezone" setting only changes display (used only in app.blade.php), not the scheduler, which is misleading. The setup scripts do write `APP_TIMEZONE`. Still present at HEAD.
  - Minor: `dynamicScreenStreamBat()` puts `request()->getHost()` into a generated .bat file. Symfony's host validation should block quote characters (unverified whether TrustHosts is enabled). The .bat prompt still says "blank to auto-read agent.toml or 1".
  - [INV] Breaks "one small step per commit" and "never mix file moves and logic changes": 84 files, script moves plus rewrites (deploy-agents.ps1 203→197 lines, stop-server.bat renamed), agent logic, server logic, gateway, Docker and installers all in one commit. Only one server test was added (`test_download_allows_bat_scripts`). There are no tests for the new gateway auth, the shared token or the agent `screen` policy parsing.
- **Undid / broke:** removed run-native.ps1 and run-server.ps1 (36/37), which drops the in-repo Windows one-shot runner in favour of start_server/windows (functionally replaced).

### 40. `da6f5e0` fix(ci): resolve agent, python ruff, and composer audit CI failures
- **Intent:** Get CI green: add X11 dev packages, add xephyguard-screen include paths to the MinGW syntax check, add missing includes, bump Symfony 7.4.22→7.4.23 for composer audit, and remove unused Python imports.
- **Touched:** .github/workflows/ci.yml, agent/apps/screen_streamer.cpp (include paths), rules.cpp, system_win.cpp, proxy test/addon (unused imports), server/composer.lock.
- **Problems:**
  - Does not weaken tests or lint. No tests deleted, no new `continue-on-error`. The only Python change is removing unused imports (`json`, `parse_qsl`, `urlencode`).
  - The CI gaps it leaves: `pint --test` still `continue-on-error: true` (known from batch 1; still at HEAD ci.yml:90–91); no Go `go test` job for the gateway (relay_test.go never runs in CI); the screen `xgtests` (including test_privacy) are not built when CI runs `cmake -S agent`. All still present at HEAD.
  - Changing `../capture/x.h` to bare includes makes screen_streamer.cpp depend on include paths set in two places (CMake and the hand-written MinGW flags in ci.yml), which can drift apart.

### Batch 4 summary
1. [PRIV][INV] Live screen streaming goes directly against docs/ETHICS.md ("screenshots or screen content: never collected"). No ETHICS/DECISIONS/PROTOCOL/THREAT_MODEL update was made, and a keyframe is cached in gateway RAM. Still present at HEAD.
2. [SEC] 4fd1a79 grants BUILTIN\Users read on all of ProgramData\XephyGuard, which exposes `agent.key` (mTLS private key), the agent.toml tokens and the shared screen token to every student. Still present at HEAD (xephyguard.nsi:196).
3. [SEC][INV] The screen channel is a second agent→CCC path that is plaintext and not mTLS. Default `ws://…:8082` on all interfaces, firewall opened, TLS off by default. It is protected only by one shared agent token handed to user-level helpers, and the agent picks its own stream id, so any PC's feed can be spoofed. Still present at HEAD.
4. [SEC] 36 installs a self-signed, exportable-key code-signing cert into LocalMachine\Root and TrustedPublisher on every PC, and committed a hard-coded PFX password plus .cer files. The password was removed in db50a52 but stays in history; the Root install and the .cer files are still present at HEAD.
5. [SEC] e1be056 gateway had an auth bypass (stream_id fallback, any agent accepted) and a hard-coded JWT secret. The source was fixed in db50a52, but dbf3148 committed gateway.exe with that secret. A stale gateway.exe is still tracked at HEAD despite .gitignore.
6. [SEC] The HelperWsClient is used by 2–3 threads with no locking (reconnect/close/receive race, worse after acd236a). Still present at HEAD.

---

## Batch 5 — commits 41–50 (2026-11-13 → 2026-11-14)

### 41. `de47069` fix(screen,proxy,ci): fix .deb CPack install rules, screen gateway routing/status, and AI Overview suppression in proxy_only mode
- **Intent:** Fix the .deb install rules, make the screen gateway accept `/screen-gw/*` paths and push agent online/offline status to viewers, and make Google AI Overview suppression work in a new `proxy_only` (PAC-based) mode.
- **Touched:** agent core (new `Policy.proxy_mode` / `proxy_pac_url`, `ISystem::set_browser_proxy_pac`), Windows `system_win.cpp` (Chromium/Firefox PAC, QUIC/DoH-off, Gemini/AI-mode policies), helper `screen_streamer.cpp` (WinHTTP NO_PROXY), proxy addon (`/async/folwr` 204, CSP relaxing, nonce-aware CSS injection, `udm14_clients`), ccc-deploy (new `proxy-entrypoint.sh`, Dockerfile CMD, nginx `/proxy.pac`), server (`/proxy.pac` route, PolicyBuilder, NetworkPolicyWriter, ProfileController), Go gateway `relay.go`, xephyguard-screen CMake, tests.
- **Problems:**
  - [INV] One commit touches six components: agent, server, proxy, ccc-deploy, xephyguard-screen, helper. The subject says "fix", but the commit adds a whole new feature (proxy_only PAC mode plus new Windows browser policies). The subject also says "ci", but no CI file changed.
  - [INV] Protocol change: the agent policy gets `proxy_mode`, `proxy_pac_url` and `screen.tls`. docs/PROTOCOL.md was not updated. No mention of proxy_only/PAC/udm14 in PROTOCOL.md or DECISIONS.md. Still present at HEAD.
  - [SEC] `proxy-entrypoint.sh` always starts a second mitmproxy listener on `0.0.0.0:8085`: an explicit (regular) proxy in gateway mode, or a transparent one in proxy_only mode. It runs with `block_global=false`. The static nftables `input` chain is `policy accept` (proxy/nftables/xephyguard.nft:33), and in proxy_only mode nft is not loaded at all. So the CCC is an open forward proxy on every interface, including WAN. Still present at HEAD (ccc-deploy/docker/proxy-entrypoint.sh:12-24).
  - [SEC] The generated PAC returns `PROXY ccc:8080; DIRECT` (PublicController::proxyPac). If the proxy is down or blocked, browsers silently go direct, so proxy_only filtering fails open. The PAC is also served and configured over plain `http://` (PolicyBuilder `'http://'.ccc_ip.'/proxy.pac'`), so anyone on the LAN can spoof it. Still present at HEAD (PublicController.php:136).
  - [SEC] `relax_style_csp` strips nonce/hash tokens from Google's `style-src` and adds `'unsafe-inline'`. This weakens the site's own CSP for every inspected Google page. Still present at HEAD (proxy/addons/xephyguard_addon.py:143).
  - [PRIV] NetworkPolicyWriter adds a PC to `udm14_clients` whenever its profile has `google_udm14`, *before* the `if (! $session) continue;` check. `wants_google_rewrite` then makes the proxy TLS-*inspect* Google for those PCs even when no session is running. DECISIONS #10 says interception is only for restricted PCs. Still present at HEAD (server/app/Services/NetworkPolicyWriter.php:36-41).
  - [UNDO] ProfileController changes the `google_udm14` default from `true` to `false`, and `wants_google_rewrite` changes the `force_udm14` fallback. Profiles saved without the box now lose AI Overview suppression. This is a behaviour change of an existing feature, made without a note. Still present at HEAD.
  - [SEC] Commit 41 replaced the image CMD with a shell script. The mitmproxy image's own entrypoint only drops to the `mitmproxy` user for `mitm*` commands, so after this commit mitmproxy ran as root, holding the CA key. Fixed in 5dc5903 (gosu), but that fix falls back to running as root if gosu is missing.
  - [SEC][HACK] docker-compose.local.yml gives the proxy container the whole `.env.local` (DB password, APP_KEY, screen-gateway secret). It only needs the network mode. Still present at HEAD (docker-compose.local.yml:82).
  - [SEC] Screen agent path: when the gateway URL is not https/443/8443, PolicyBuilder now explicitly sends `path=/agent, tls=false`. The agent then streams screen content over plain `ws://ccc:8082`. This is a non-mTLS agent→CCC channel carrying the most sensitive data. Still present at HEAD (install.sh sets SCREEN_GATEWAY_PORT=8082).
  - Bug: `dynamicScreenStreamBat` computes `$tls` from the gateway URL but then takes the scheme from `request()->isSecure()`. A TLS gateway reached from an http page gives `ws://…:443/screen-gw/agent`. Still present at HEAD (PublicController.php:168).
  - Bug (unverified): `CMAKE_SKIP_INSTALL_RULES` changed from a FORCE'd cache variable to a normal variable inside xephyguard-screen. Whether this still suppresses libjpeg-turbo's install rules without hitting the agent's CPack rules was not run.
  - [HACK] Copy-paste: the offline-status JSON is built twice in relay.go (`broadcastStatusLocked` and the viewer timer). One stale comment ("Defer notifying offline…") sits above the keyframe code.
  - Windows registry/PAC code: compile-checked only, never run on Windows. The PAC URL comes from the server and is written to HKLM without any validation.

### 42. `5dc5903` Fix proxy mitmproxy CA confdir/UID, local root-CA seeding, and addon EventShipper test
- **Intent:** Make mitmproxy find its CA (explicit confdir, HOME), drop to the mitmproxy UID via gosu, and seed the local dev proxy from the lab root CA (`ca.key` + `ca.crt`).
- **Touched:** ccc-deploy/bootstrap-local.sh, ccc-deploy/docker/proxy-entrypoint.sh, proxy/tests/test_udm14_and_csp.py.
- **Problems:**
  - [SEC] Local bootstrap now builds `mitmproxy-ca.pem` from the **lab root CA private key** (`cat ca.key ca.crt`). The same key signs agent mTLS certificates and signature bundles (DECISIONS #6). The same `cat` already existed in install.sh, and DECISIONS #11 records it ("mitmproxy signs with the lab root CA … revisit"). Even so, the most exposed container (internet-facing, decrypting TLS, open listener per commit 41) holds the key that could mint mTLS client/server certs and forged signature bundles. Permissions themselves are correct (dir 0700, key file 0600, uid 1500). Still present at HEAD (bootstrap-local.sh:58-59, install.sh:168).
  - [SEC] Entrypoint fallback: when not root or gosu is missing it just `exec`s, so mitmproxy runs as root in that case. The `chown -R … /var/log/xephyguard 2>/dev/null || true` hides errors. Still present at HEAD.
  - Positive: the test now also exercises the CSP/udm=14 response path. No test was weakened.

### 43. `31594cf` Clear cached keyframe on agent disconnect and viewer canvas on offline status
- **Intent:** Stop the gateway from replaying a stale screen frame after an agent disconnects or reconnects, and clear the viewer canvas when the agent goes offline.
- **Touched:** xephyguard-screen/gateway/relay.go, relay_status_test.go, server/resources/js/app.js.
- **Problems:**
  - None in the diff. It is a privacy improvement: the last screen frame is no longer kept in gateway memory after a disconnect.
- **Undid / broke:** The app.js part of this fix (`tileCanvas.clear()` in `onControlMessage`) was deleted one day later by abfc9df (48), together with the whole viewer wiring. A reduced reimplementation exists at HEAD in server/public/js/app.js.

### 44. `7216fa3` fix(signatures,agent): validate bundle domains, auto-publish, guard bad bundles, and sync helper ext_ids
- **Intent:** Refuse to sign or publish bundles with empty AI/IDE domain lists. The agent keeps the last good bundle and backs off 15 min on a refused version. Signatures get auto-published every minute. The agent pushes enabled extension IDs to the helper.
- **Touched:** agent core (agent.cpp/hpp, rules.cpp/hpp, test), server SignatureCompiler, SignatureController, PublishSignatures command, Bootstrap, routes/console.php scheduler, new feature test.
- **Problems:**
  - [SEC][INV] New scheduler `xg-signatures-autopublish` runs `publish(null,'auto')` every minute. Any rule or blocklist edit is now compiled, **signed with the CA key and activated within a minute, with no review step**. That makes the manual "Publish" button (with its note) pointless. The auto path also writes **no `Audit::log`** and sends no `policy_updated`. The controller path does both, so auto-signed versions leave no audit trail. This is a feature behaviour change made without approval. Still present at HEAD (server/routes/console.php:13-22).
  - [HACK] Half-finished: `ext_ids` is pushed in the helper config, and the comment says "the WSL helper uses it". At HEAD, nothing outside agent core reads `ext_ids` (git grep finds only agent/core, tests, data/). The helper side was never written. This also changes the agent↔helper message format without documentation. Still present at HEAD.
  - [INV] Agent and server changed in one commit.
  - Inconsistency: the server refuses a bundle if *either* `domains.ai` or `domains.ide` is empty. The agent only refuses if *both* are empty, and only when it already has AI domains. Still present at HEAD.
  - Note: cached and seed bundles are still loaded with `require_signature=false` (agent.cpp:44). This predates the commit.

### 45. `964b4a7` ci(windows): build and upload only xephyguard-agent.exe
- **Intent:** Speed up or unbreak the MSVC CI job.
- **Touched:** .github/workflows/ci.yml (agent-windows-msvc job).
- **Problems:**
  - [INV][UNDO] The job now passes `-DXG_BUILD_TESTS=OFF` and removes the `ctest` step. The Windows unit tests, the only native Windows test run, were dropped without approval. Still present at HEAD (ci.yml:42, no ctest in the Windows job).
  - [INV][UNDO] `--target xephyguard-agent` stopped the MSVC build of `xephyguard-helper` (session helper + screen streamer). For this window only the mingw `-fsyntax-only` check covered the helper. Fixed in 80bfcbf (helper target re-added), but tests are still off.

### 46. `4ac6cd8` feat(ui): add UI v2 static design system assets, fonts, icons, and scripts
- **Intent:** Add UI v2 static assets: xg.css, three woff2 fonts, vendored Alpine, a static-demo `public/js/app.js` and `public/js/data.js`, and a new resources/css/app.css.
- **Touched:** server/public/{css,fonts,js}, server/resources/css/app.css (Tailwind `@import`/`@source` removed).
- **Problems:**
  - [HACK] `public/js/app.js` header: "interactive preview logic (static demo, no backend)". `data.js` is hard-coded demo data: 16 fake PCs, alerts, processes, tabs, pastes and key rates. It is served publicly at `/js/data.js`. data.js is still in server/public/js at HEAD and is imported by resources/js/app.js, but no view loads it (dead, publicly reachable). Still present at HEAD.
  - [INV] data.js hard-codes AI domains and rule IDs (`cdn.grok.com`, `api.openai.com`, `chatgpt.com`, an extension ID). Signatures belong in data/. Still present at HEAD (demo-only file).
  - [PRIV] Demo data uses what look like real names and institution details: PCs "Rupom2", "SIyam", "Sazzad", "KUET admin", `files.kuet.ac.bd`. Whether these are real people is unverified. Still present at HEAD (server/public/js/data.js:11,32,75).
  - Third-party code: `alpine.min.js` (Alpine.js 3.14.8, MIT) is vendored with no licence header. It duplicates the npm `alpinejs ^3.17.5` dependency at a different version. Fonts Inter and IBM Plex Mono (SIL OFL) were added without licence files. Still present at HEAD.
  - Typo in the demo: process `xerophyte-agent`.

### 47. `5269f30` feat(ui): implement UI v2 master shell layout, login, student notice, and setup wizard
- **Intent:** Replace the Tailwind layout, login, notice and wizard with UI v2 markup.
- **Touched:** layouts/app.blade.php, auth/login.blade.php, public/notice.blade.php, setup/wizard.blade.php.
- **Problems:**
  - [UNDO][HACK] The layout became a static mockup. It dropped `@vite` (so the real Alpine/Echo/ScreenViewer bundle no longer loaded), the CSRF and Reverb meta tags, role-based nav filtering, the logout form (replaced by a link to `login.html`) and flash/error output. All links pointed to non-existent `*.html` pages. The status bar was fake: "Exam · CSER · 41 min left", "Server 192.168.0.51", "14 of 15 PCs online", "6" alerts, "KUET admin". Fixed in bbbf38e / 259a359 (dynamic layout, role-gated nav, logout form, `.html` aliases).
  - [SEC][UNDO] The login page no longer authenticated anyone. The form had no `action`, no `@csrf` and no `name` attributes, and `onsubmit` jumped to `index.html` ("Interactive preview… no account is checked"). Fixed later (HEAD login.blade.php:24 posts to /login with @csrf).
  - [PRIV] The login view hard-coded the maintainer's personal email (address redacted) as the default value. Gone at HEAD, but it remains in git history.
  - [PRIV][UNDO] The student notice was rewritten. It dropped the admin-configured `$text` (restored at HEAD) and the lab name. It also dropped rows and statements that are still missing at HEAD: the **window titles** row, the **blocked website attempts** row, the **research/consent** statement and the **retention** statement. It adds "Your screen, when an instructor opens it — Test only" and "Screens are viewed live, never recorded". At HEAD, `ViewTokenController::issuePcToken` allows viewing "for ad-hoc / lab supervision" with no active session, and the UI offers "Take snapshot". So the notice understates what is collected and contradicts docs/ETHICS.md:24 ("Never collected: … screenshots or screen content"). Still present at HEAD.
  - The wizard dropped the `$errors` display. Fixed later (HEAD wizard.blade.php:20).

### 48. `abfc9df` feat(ui): implement UI v2 Live lab seat map, screen wall, dock, and 9-tab PC inspector
- **Intent:** Rebuild the Live lab page as the UI v2 seat map, screen wall and inspector.
- **Touched:** server/resources/js/app.js (727 lines), dashboard/live.blade.php (682 lines).
- **Problems:**
  - [UNDO] resources/js/app.js was overwritten with a copy of the demo `data.js`. This deleted the real Alpine bootstrap, Echo/Reverb import, `ScreenViewer` wiring, CSRF `xgApi()` helper and the `labView` component, including 43's fix one day earlier. live.blade.php has zero server bindings (no `$`, `route()`, `@csrf`). So the Live lab, isolation ("Cut off network"), messaging and screen view were all non-functional demo widgets. Partly fixed later (a811978, bbbf38e, e7e9af7): a smaller ScreenViewer now lives in public/js/app.js. At HEAD, resources/js/echo.js and resources/js/screen/viewer.js are not loaded by any view, so Reverb push is gone (unverified whether polling fully replaces it).
  - [PRIV] New UI surfaces need an ETHICS check and got none: a "Screen wall" of *all* PCs ("thumbnails update about once a second"), a "Take snapshot" button, a browser **Tabs** tab (tab title + host per open tab), and a pastes table with a **From** column (paste source). ETHICS.md says no screen content, and window titles are off or app name by default. Still present at HEAD (live.blade.php:284, 352, 397). Where the HEAD data for these comes from is unverified.
  - [INV] One commit with ~1,400 changed lines. It mixes deleting working code with adding a mockup.

### 49. `3bcde4a` feat(ui): implement UI v2 Alerts, Sessions, Shared clipboard, and Downloads views
- **Intent:** UI v2 views for Alerts, Sessions, Shared clipboard and Downloads.
- **Touched:** PublicController.php (−292 lines), alerts/index, sessions/index, public/downloads, public/share Blade views.
- **Problems:**
  - [UNDO] `downloadsIndex()` lost its real logic (directory scan, filename regex, installer metadata, credentials). It became a static view with fake sizes and toasts saying "File i…". Fixed later in a811978 / e9ded61 ("Restore connection credentials … on /downloads").
  - [SEC][HACK] The static downloads page hard-codes an enrollment key `XG-A77CFB-6554C8-489B53` and `http://192.168.0.51:8000`, shown as a `curl … | sudo bash` command over plain HTTP. Whether the key is real is unverified; it already appeared in earlier commits (04a0d3b, db50a52). Gone at HEAD but still in git history.
  - [SEC] Known `/share` exposure (batch 1): this commit added no auth. Routes GET `/share`, POST `/share` and GET `/share/raw` are still outside any auth group. The new view introduces a **share history** list, and later commits (a811978 onward) persist it as `shared_clipboard_history` (20 entries). That history is returned by unauthenticated `/share/raw`, and unauthenticated POST `/share` lets anyone (including students) overwrite the text pushed "to all PCs" and mark it as a command. The exposure grew. Still present at HEAD (routes/web.php:27-30, PublicController.php:187-221).
  - [HACK] The alerts and sessions views are pure demo: they bind to `XG.alerts`, hard-coded "CSER lab exam" and "KUET admin", and have no `$` or `@csrf`. Acknowledging alerts did nothing. Fixed later (a811978).

### 50. `5681d2d` feat(ui): implement UI v2 Profiles & Timetable, Roster, Blocklists & Rules, and Session Reports views
- **Intent:** UI v2 views for Profiles/Timetable, Roster, Blocklists & Rules, Reports and the Session report, plus a `showByNumber` report controller action.
- **Touched:** ReportController.php, profiles/index, reports/index, reports/session, new reports/session-2, roster/index, signatures/index.
- **Problems:**
  - [UNDO][HACK] All five working views (previously with forms, `@csrf` and `route()` calls; signatures had 6, roster 3) were replaced by static mockups with zero server bindings. Profile/timetable editing, roster import, blocklist/rule toggles, publish and report re-score/CSV became toast-only buttons. Fixed later (53e7d8e).
  - [INV] The signatures mockup hard-codes AI/IDE domain lists (`chatgpt.com`, `claude.ai`, `aistudio.google.com`, `api.githubcopilot.com`, `codeium.com`, `cdn.grok.com`, `ai21.com`, `replit.com`, …) in the server view instead of reading data/. Gone from views at HEAD (only the dead data.js keeps some).
  - [HACK] `ReportController::showByNumber()` was added with no route in this commit and simply rendered a static `session-2` demo report. At HEAD it is routed (`/report-session-{num}.html`) and loads `LabSession::find($num)`. When no session matches, it renders `reports.session` with no data instead of a 404. Still present at HEAD (ReportController.php:57-63).
  - [INV] The demo report states "Screen snapshots are taken only when an instructor asks for one", which contradicts ETHICS.md ("never … screenshots"). Report wording at HEAD was not checked (unverified).
  - Roster demo names (Nusrat Jahan, Rupom Hasan, …) come from earlier test fixtures (a8124dc) and look fictional. Removed from views in e7e9af7.

### Batch 5 summary
1. [SEC][INV] 44 auto-publishes signed signature bundles every minute with no review and no audit log, so any rule edit goes live to every agent. Still present at HEAD.
2. [SEC] 41 runs mitmproxy as an explicit proxy on 0.0.0.0:8085 (and 8080 in proxy_only mode) with `block_global=false` while the nft input chain accepts everything: an open proxy on WAN. The proxy_only PAC fails open (`; DIRECT`) and is served over plain HTTP. 42 hands this container the root CA key that also signs mTLS certs and bundles. Still present at HEAD.
3. [PRIV] The 47 student notice dropped the window-title, blocked-site, research and retention disclosures, and claims screen view is Test-only and never recorded. HEAD allows ad-hoc viewing outside sessions and snapshots. 48 adds a screen wall, tab titles and paste source with no ETHICS check. Still present at HEAD.
4. [SEC] The unauthenticated `/share` exposure grew: 49 adds share history, later persisted and readable via `/share/raw`, and anyone can POST text or commands to all PCs. Still present at HEAD.
5. [UNDO][HACK] 47–50 replaced working views and JS with static demos: fake login with no auth, the maintainer's email hard-coded, real Live lab/Reverb/viewer code deleted, a hard-coded enrollment key. Mostly fixed by bbbf38e/a811978/53e7d8e/e7e9af7. The email and key are still in git history, and demo data.js is still public at HEAD.
6. [INV] 45 dropped Windows ctest (still off at HEAD) and the helper build (re-added in 80bfcbf). 44's helper `ext_ids` has no consumer at HEAD. 41's new policy fields are missing from PROTOCOL.md.

---

## Batch 6 — commits 51–60 (2026-11-14 → 2026-11-14)

All ten commits landed on the same day. No commit in this batch touched docs/ (PROTOCOL, THREAT_MODEL, ETHICS, DECISIONS, FEATURES, PLAN).

### 51. `259a359` feat(ui): implement UI v2 Enrollment, Health, Settings, Staff Accounts, Audit views and .html route aliases
- **Intent:** Add static UI v2 pages for Enrollment, Health, Settings, Staff accounts and Audit log. Add `*.html` route aliases so links from the static design mock keep working.
- **Touched:** `server/routes/web.php` (+21 alias routes), `admin/{audit,health,settings,users}.blade.php`, `enroll/index.blade.php`.
- **Problems:**
  - Route check: every `.html` alias sits in the same middleware group as its original route. `/index.html`, `/alerts.html`, `/sessions.html` and `/enroll.html` are under `auth` + `role:assistant,instructor`. `/profiles.html`, `/reports.html`, `/report-session-{num}.html` and `/roster.html` are under `role:instructor`. `/signatures.html` and `/health|settings|users|audit.html` (plus the `admin-*.html` duplicates) are under `role:admin`. `/notice.html`, `/downloads.html` and `/share.html` are public, the same as their originals. **No auth or role bypass.** Still present at HEAD (web.php:20–133).
  - [HACK] `/login.html` (web.php:37) does not have the `guest` middleware that `/login` has, so a signed-in user can still open the login page. Harmless, but inconsistent. Still present at HEAD.
  - [HACK] Each admin page has two or three URLs (`/health`, `/health.html`, `/admin-health.html`). This leftover mock-compat surface is never cleaned up. Still present at HEAD.
  - [HACK] The views are static mock pages with hard-coded fake data: enrolment key `XG-A77CFB-…`, a fake CA fingerprint, the CCC URL `http://192.168.0.51:8000`, fake staff accounts that include the developer's personal gmail address, and fake audit rows. Wired in 55, and the fallbacks were removed in e7e9af7 (gone at HEAD).
  - The audit view used `x-html="r.json"` (an XSS sink). In 55 the input was escaped with `e()` first, so it was not exploitable. It was replaced in d9dc579 (gone at HEAD).
  - [INV] One commit mixes 5 views with route changes (two kinds of change in one step).

### 52. `bbbf38e` feat(ui): wire layout, auth, role-gated nav, base href, and live ScreenViewer runtime
- **Intent:** Connect the layout to the real user, roles and lab stats. Add a real login form, CSRF meta and `<base href="/">`. Add the TileCanvas/ScreenViewer WebSocket runtime to `public/js/app.js`.
- **Touched:** `layouts/app.blade.php`, `auth/login.blade.php`, `public/js/app.js` (+485), `resources/js/app.js` (demo data removed), `Models/Lab.php`.
- **Problems:**
  - [SEC] `$canInstructor = ! $xgUser || …` and `$canAdmin = ! $xgUser || …`, with `$xgRole` defaulting to `'admin'`. Guests on the public `/downloads` and `/share` pages saw the full instructor and admin nav. The routes were still protected, so this exposed the page list only. Fixed in e7e9af7.
  - [HACK] The layout `@php` block wraps all DB queries in `catch (\Throwable)` and falls back to fake numbers (`$xgOpenAlerts = 6`, `15` PCs, `14` online, server `192.168.0.51`, signature `2026.11.08-b2a25a`). The footer also hard-coded "Live updates connected". Fake values fixed in e7e9af7. The silent `catch (\Throwable)` is still present at HEAD (now with zero/blank values).
  - [HACK] The login form pre-filled the developer's personal email as the default value. Fixed in e7e9af7.
  - [SEC] `Lab::enrollKey()` now swallows decrypt failures and returns null. If APP_KEY changes, the enrolment key silently disappears instead of raising an error. Still present at HEAD (Lab.php:37).
  - Window globals check: `<meta name="reverb-key">` exposes only the Reverb public app key, which is public by design. No Reverb secret or token is exposed. `window.xgApi` and `window.xgCsrf` read the CSRF token from the meta tag. The screen viewer receives a short-lived token from `POST /api/screen/pc/{id}/token` and puts it in the WebSocket URL query (`/view?token=…`). That puts the token where proxy and access logs can record it, and the URL falls back to `ws:` (plaintext) when the dashboard is on http. Unverified whether production is https-only. Still present at HEAD (app.js:858, 953).
  - [HACK] The live view fell back to the demo `XG.pcs`/`XG.alerts` from `public/js/data.js` when no live PCs existed. Fallback removed in e7e9af7. `public/js/data.js` (fake students, fake alerts) is still served publicly at HEAD, although nothing loads it any more.
  - [PRIV] `xgDraw` draws *synthetic student desktops* (fake code editor, browser, terminal) on each tile until a real frame arrives. On the real wired dashboard (53+) an instructor could take these for the student's actual screen. Gated behind `window.XG_DEMO`, which is never set, in 4ac2d8e (fixed at HEAD; dead demo code remains).

### 53. `a811978` feat(ui): wire Live lab, Alerts, Sessions, Shared clipboard, Downloads, and Notice to backend
- **Intent:** Feed real controller data into the Live lab, Alerts, Sessions, Share, Downloads and Notice views.
- **Touched:** `DashboardController` (adds hostname/ip/os/capabilities/agent version to tiles), `PublicController` (downloads data, share history), `Setting::set()` alias, the matching views.
- **Problems:**
  - [SEC][PRIV] **Known `/share` issue, made worse.** `shareSave` now keeps a **history of the last 20 shared texts (2000 chars each)** in `settings.shared_clipboard_history`. The unauthenticated `GET /share` and `GET /share/raw` return it, and the page polls `/share/raw` every 2.5 s. Anyone who can reach the page can read and post. CSRF applies, but a guest gets a token from the page. There is no throttle. During an exam this works as an anonymous, persistent message board between lab PCs. Anonymous posts are labelled "by Instructor" (`auth()->user()?->name ?: 'Instructor'`), so students can impersonate staff. The stored text is text people post on purpose, not captured student clipboard, so it does not directly break "clipboard = size only". Still present at HEAD (PublicController.php:187–221).
  - Before this commit `shareSave` called `Setting::set()`, which did not exist, so saving was broken. The new `set()` alias fixes it.
  - [SEC] The public `/downloads` page now gets the **real lab enrolment key** (`$lab->enrollKey()`) and prints it in the install commands. This exposure already existed before UI v2 (04a0d3b/3bcde4a^), and this commit carries it over. Auto-approve is MAC-gated (EnrollmentService.php:56), so a leaked key still needs manual approval or a whitelisted MAC. The commit also added a fake key fallback `'XG-A77CFB-…'`, removed in e7e9af7. The key exposure is still present at HEAD (PublicController.php:78, downloads.blade.php:9–24).
  - [SEC] The Linux install command is `curl -fsSL http://…/install/linux.sh | sudo bash`. The script carries the CA fingerprint, so fetching it over plaintext lets a LAN MITM swap the pin and the script that runs as root. In this commit the scheme followed the request. At HEAD it is forced to `http://` (`$httpBaseUrl`, added 4ac2d8e). Still present at HEAD.
  - [HACK] Fake fallbacks are mixed into the real views: "Exam · CSER · 41 min left" when no session is running, demo share history, `'KUET admin'` as the alert acknowledger, `'0.2.0'` agent version, fake session options. Removed in e7e9af7.
  - [HACK] Live tile status thresholds (`score >= 50` alert, `>= 25` watch) are hard-coded in Blade, separate from the server and analysis scoring. Unverified whether they match the scorer.
  - [HACK] The alert ack and user save were fire-and-forget `xgApi` calls that showed a success toast even when the request failed. Fixed in d9dc579 (awaited, with error toasts).
  - [PRIV] (unverified) The Programs tab shows `p.title || p.cmdline` and the browser-tab titles. ETHICS.md allows window titles only as configurable (default off / app name) and does not list process command lines. Whether the agent sends cmdline depends on agent code outside this batch.

### 54. `53e7d8e` feat(ui): wire Profiles, Timetable, Roster, Blocklists & Rules, and Session/Student Reports
- **Intent:** Connect the Profiles, Timetable, Roster, Signatures and Report views to the backend. Let blocklist import accept pasted text as well as a file.
- **Touched:** `ProfileController` (kill_rules split on whitespace and commas), `SignatureController::import` (new `text` input, max 200 000 chars), the views for profiles, signatures, roster, reports index and reports session.
- **Problems:**
  - [UNDO][INV] The new HTML session report **drops traceability that the old report had** (`reports/_body.blade.php`, still used for PDF): the "Not observable" column, the signal IDs behind each reason, alert review comments, the "scoring failed: …" error message, the "looks like code" flag for pastes, and the warning that exempt students and boilerplate can raise signals legitimately. Still present at HEAD (session.blade.php has none of these).
  - [INV] The HTML report shows an inverted **"integrity" score** (`100 - score`) with coloured "low" bars. The PDF and CSV show the raw score. The same student gets e.g. 85/100 on screen and 15/100 in the PDF, which confuses the human review. When a reason has no points it shows a made-up `+5` (`$r['points'] ?? $r['weight'] ?? 5`), and when confidence is missing it shows `'Medium'`. Still present at HEAD (session.blade.php:31, 46, 51).
  - [INV][HACK] Fallback data **hard-codes blocklist and rule content in Blade** (`chatgpt.com`, `claude.ai`, `cdn.grok.com`, `proc.ollama`, `web.grok`, counts `all: 285`), shown as if it were real policy when the DB is empty. It also hard-codes realistic fake student names, IDs and consent states in the roster. Removed in e7e9af7 (only placeholders remain at HEAD).
  - The roster defaults consent to `'declined'` when none is recorded and pseudonymisation to on. That is the safe default.
  - Blocklist import accepts up to 200 KB of pasted text (validated; admin-only route). OK.

### 55. `dda63f7` feat(ui): wire Enrollment, Health, Settings, Staff Accounts, and Audit Log to backend
- **Intent:** Connect the remaining admin pages and the enrolment page to the backend.
- **Touched:** `AdminController::audit`, `Services/Audit.php`, `admin/*`, `enroll/index.blade.php`, `setup/wizard.blade.php`.
- **Problems:**
  - [SEC][INV] **`Audit::log()` now swallows every exception**: `catch (\Throwable) { return new AuditLog($attrs); }`. If the insert fails, the staff action (screen view, isolate, kill, user change, settings change) still goes through with no audit record and no error. This silently weakens the accountability the audit log exists for. `AdminController::audit()` also catches `\Throwable` and shows an empty list instead of the DB error. Both still present at HEAD (Audit.php, AdminController.php:121).
  - [HACK] Every page still falls back to fake data when the DB is empty: fake audit rows, fake staff list with a personal email, fake enrolment key and **fake CA fingerprint** inside a copy-paste install command, fake health checks ("CA valid until 2036", "Backups 03:00 today"), fake disk usage (142/512 GB), fake agent-version stats, `auto_approve` defaulting to **true**. A health page that shows green checks on fake data is misleading. Removed in e7e9af7. HEAD defaults auto-approve to false.
  - [PRIV] The settings view defaulted `log_urls.enabled` to `true` (`?? true`) and `title_capture_test` to `'full'`. The controller always passes `log_urls` (default false), so the true default only applied if that key was missing. Changed to `?? false` in d9dc579. The `'full'` test default is still present at HEAD (ETHICS.md allows "configurable" for test).
  - Role gating checked. Staff accounts, settings and audit are under `role:admin`. Enrolment approve and reject are under `role:assistant,instructor`, which matches the original design. Rotate key, auto-approve and retire are admin-only. `saveUser` validates its input, refuses self-demotion, and assigns fields one by one (no mass assignment). OK at HEAD.
  - Script globals: the enrolment page puts the real enrolment key and CA fingerprint into Alpine state. Only staff can see that page.

### 56. `31fc124` fix(ui): hydrate Alpine state via script globals and support demo report fallback on /reports/{id}
- **Intent:** Move server data into inline `<script>window.XG_*_PAGE = {…}</script>` globals. Show the demo report on `/reports/1` and `/reports/2` when there are no sessions.
- **Touched:** `ReportController::show`, 10 views.
- **Problems:**
  - [HACK] `ReportController::show(string|int $session)` dropped route-model binding so it could serve **demo reports in production** when `LabSession::count() === 0`. Reverted in e7e9af7 (gone at HEAD).
  - Script globals check: the globals hold page data (users without password hashes, audit rows, alerts, enrolment data, roster rows, signature lists, share history). All are emitted with `@json`, which uses the HEX_TAG/APOS/QUOT/AMP flags, so there is no `</script>` breakout. No Reverb secret, API token or password. OK.
  - [UNDO] Adding inline `<script>` blocks clashed with the nginx CSP (`script-src 'self' 'unsafe-eval'`, no `unsafe-inline`), so the pages broke in production. Fixed in d9dc579.
  - [HACK] More fake fallbacks (`byName: 'KUET admin'`, roster `total: 65`). Removed in e7e9af7.

### 57. `e7e9af7` fix(ui): remove hardcoded demo fallbacks and fix live seat map & detail hydration
- **Intent:** Remove all demo and fallback data from views and controllers. Fix seat-map and PC-detail hydration.
- **Touched:** 24 files: controllers (Dashboard, Public, Report), layout, login, all UI v2 views. Deletes `reports/session-2.blade.php`.
- **Problems:**
  - Mostly a fix. It removes the fake key, fingerprint, emails, students, blocklists, health checks and the guest-sees-admin-nav defaults. Verified: no demo strings at HEAD.
  - `showByNumber` now does `LabSession::find($num)`, which makes `/report-session-{n}.html` a second URL for `/reports/{n}`. It is instructor-gated like the original route. When no session exists it renders the report view with no data, which shows "Session not found" at HEAD. OK.
  - Left behind: `public/js/data.js` (unused demo dataset) and the synthetic-desktop drawer in app.js. See 52.
  - [INV] One commit spans 24 files of mixed fixes.

### 58. `d9dc579` Fix UI synchronization, CSP runtime blocking, settings validation, and pagination bugs
- **Intent:** Make the UI work under the existing CSP: move inline scripts to `public/js/app.js` and `theme.js` and pass data in `<script type="application/json">` blocks. Fix settings validation, alert pagination and counts, and return JSON from many controllers. Add `UiRegressionTest`.
- **Touched:** 29 files, +1248 lines: 7 controllers, `app.js` (+755), every view, a new feature test file.
- **Problems:**
  - CSP check: the CSP in `ccc-deploy/nginx/xephyguard.conf:41` is unchanged (`script-src 'self' 'unsafe-eval'`, `style-src 'unsafe-inline'`, `connect-src ws: wss:`). `'unsafe-eval'` has been there since the initial commit 51649d2; Alpine's standard build needs it. This commit does **not** weaken the CSP. It removes every inline `<script>` and `on*=` handler (0 left at HEAD) and adds a test that checks this. JSON blocks use `json_encode(..., JSON_HEX_TAG|AMP|APOS|QUOT)` inside `{!! !!}`, which is safe. Still present at HEAD.
  - [SEC] `google_search_url` validation was loosened from `url` to `regex:#^https?://\S+$#i` so that `{searchTerms}` is accepted. Any `http://` URL is now accepted, and it is unverified how that value reaches agents or policy. Minor; admin-only. Still present at HEAD (AdminController.php:61).
  - Good: `log_queries` can only be on when `log_urls` is on; alerts paginate with server-side severity totals; ack handles PCs with no pc row; user save, alert ack and enrolment are now awaited and show errors.
  - The dashboard now puts up to 500 unacknowledged and 30 acknowledged alerts into the page JSON on every load. A performance risk, not a bug.
  - [INV] A huge mixed commit (CSP refactor + validation + pagination + JSON API responses + tests) breaks "one small step per commit". The tests only cover settings, alerts and CSP. Nothing tests that `.html` aliases or `/share` respect auth.

### 59. `80bfcbf` ci(windows): package single XephyGuard-Setup installer via NSIS
- **Intent:** Build the agent and helper on windows-2022, compile `data/` signatures to JSON, build `XephyGuard-Setup-<ver>.exe` with NSIS, and upload that instead of the raw agent exe.
- **Touched:** `.github/workflows/ci.yml` (Windows MSVC job).
- **Problems:**
  - [HACK] The version regex `'VERSION\s+([0-9.]+)'` first matches `cmake_minimum_required(VERSION 3.20)`, so the installer would be named and stamped **3.20**. Fixed in 686ff42.
  - [HACK] If the regex fails, the version silently falls back to `'0.1.0'` instead of failing the build. Still present at HEAD (ci.yml).
  - [SEC] The installer is **not code-signed** (no signtool step), so an unsigned installer is shipped as the release artifact. No CA, key or test cert is bundled. The NSIS defaults for `DEFAULT_KEY` and `DEFAULT_CAFP` are empty, so the CA pin comes from `/CAFP=` at install time. Still present at HEAD.
  - [INV] New CI tooling: `pip install pyyaml` (unpinned) and `choco install nsis` (unpinned). These are new CI dependencies added without a recorded decision. Windows tests are still not built (`-DXG_BUILD_TESTS=OFF`, pre-existing), and the README calls the Windows installer untested on real Windows. The raw `xephyguard-agent.exe` artifact is no longer uploaded (replaced, not lost). No other jobs were dropped.
  - Signatures are compiled from `data/` YAML (`scripts/compile_signatures.py`), which keeps the "signatures live in data/" rule. OK.

### 60. `686ff42` ci(windows): fix project version regex for NSIS installer packaging
- **Intent:** Anchor the version regex to `project(xephyguard_agent VERSION x.y.z`, add `setup-python` 3.12 and `PYTHONUTF8`, and split signature compilation and NSIS into separate steps.
- **Touched:** `.github/workflows/ci.yml`.
- **Problems:**
  - Fixes the 3.20 version bug from 59. Checked: `agent/CMakeLists.txt:2` is `project(xephyguard_agent VERSION 0.1.0 …)`.
  - The silent `'0.1.0'` fallback, the missing signing step and the unpinned NSIS/pyyaml from 59 remain. Still present at HEAD.

### Batch 6 summary
1. **[SEC][PRIV] The public `/share` board gets worse (53).** It now keeps a 20-entry, 2000-char text history. Unauthenticated `/share` and `/share/raw` serve it, polled every 2.5 s. There is no throttle, and anonymous posts are labelled "by Instructor". It works as an anonymous message channel between lab PCs during exams. Still present at HEAD.
2. **[SEC][INV] `Audit::log()` swallows all exceptions (55).** If the insert fails, staff actions still go through with no audit record and no error. Still present at HEAD.
3. **[INV][UNDO] The HTML session report lost traceability (54).** It dropped the "not observable" column, signal IDs, review comments, the scoring-failure message and the exempt-student warning. It also shows an inverted "integrity" score that does not match the PDF/CSV, plus a made-up `+5` and a default "Medium" confidence. Still present at HEAD.
4. **[SEC] Bootstrap trust (53, carried over).** The public `/downloads` page shows the real enrolment key. The Linux install command pipes `http://…/linux.sh` (which carries the CA pin) into `sudo bash`, and HEAD forces http. Still present at HEAD.
5. **[HACK] Fake data shown in production (51–56).** The fake enrolment key and CA fingerprint, fake health checks, fake roster and blocklists, demo reports, and guest-visible admin nav were fixed in e7e9af7. The synthetic student desktops were gated in 4ac2d8e. The unused `public/js/data.js` is still served at HEAD.
6. **[SEC] Unsigned Windows installer from CI (59/60).** It falls back silently to version `0.1.0`, NSIS and pyyaml are unpinned, and Windows tests are still off. Still present at HEAD.

---

## Batch 7 — commits 61–65 (2026-11-14 → 2026-11-15)

### 61. `6e94215` Fix /downloads layout for unauthenticated views and release artifact presentation
- **Intent:** Make the public /downloads page look right for guests (no side rail), show file sizes in KB/MB, and list the extra release files in their own "Agent binaries and helpers" panel.
- **Touched:** server `PublicController::downloadsIndex` (file list with `size_label`), `layouts/app.blade.php` (`no-rail` class, active Downloads link), `public/downloads.blade.php`, `xg.css`, `UiRegressionTest`.
- **Problems:**
  - [SEC] The page already put the plain lab enrollment key into the install commands (`$winCmd`, `$linCmd`) for anyone who opens it. This is the known "public /downloads" problem. The commit adds a guest-specific layout and a guest test (`test_downloads_layout_handles_guest_no_rail_and_kb_sizes`), so guest access is now designed in on purpose, not left in by accident. Still present at HEAD (`PublicController.php:78` passes `'key' => $lab?->enrollKey()`).
  - Bug: the new panel labels `.cer` files "Code signing certificate" and links them to `/downloads/{name}`. The `download()` route only accepts `deb|rpm|msi|exe|bat|sha256` (`PublicController.php:87`), so `.cer` links return 404. The test only checks the label text, not the download. Still present at HEAD (`downloads.blade.php:84`).
- **Undid / broke:** nothing.

### 62. `e9ded61` Restore connection credentials and /CAFP parameter on /downloads and /enroll
- **Intent:** Add a "Connection credentials" panel (CCC host, lab enrollment key, CA SHA-256 fingerprint, each with a Copy button) to /downloads. Add `/CAFP=<fingerprint>` to the Windows silent-install command, and make `/CCC=` use the bare host on both /downloads and /enroll.
- **Touched:** `public/downloads.blade.php`, `enroll/index.blade.php`, `public/js/app.js` (`dlPage`, enroll `cmd`), `UiRegressionTest`.
- **Problems:**
  - [SEC][INV] The lab enrollment key (a secret) is now shown in large type with a Copy button on the **unauthenticated** /downloads page (`downloads.blade.php:52`). It is also embedded in the page JSON (`'key' => $enrollKey`). Any student or lab-network user can read it. The guest test asserts this on purpose: `assertNotEmpty($dlData['key'])` (`UiRegressionTest.php:199`). It contradicts the model comment "shown on the Add-a-PC page to staff only" (`Lab.php:32`). Effect: anyone with the key can call `POST` enroll with their own CSR and create a pending enrollment, which can flood the queue or pose as a real PC hostname (a re-enroll marks older rows for that hostname as `superseded`). Staff approval is still needed unless `auto_approve` is on. Auto-approve only checks that a reported MAC is on the allow list (`EnrollmentService.php:56`), and the agent reports that MAC itself, so it can be spoofed and a rogue agent could be signed with no human involved. Still present at HEAD.
  - [SEC] The CA fingerprint is public, so showing it is fine. But it is delivered on the same page as the installer, and that page can be plain HTTP (see commit 65, which forces `http://` for the Linux install command). So `/CAFP` does not give a separate trust anchor: an attacker who can change the page can change both the installer and the fingerprint.
  - The enrollment key is now shown in three places on one page (commands, JSON, credentials panel). That is more exposure than the old flow needed.
- **Undid / broke:** The subject says "Restore", but this batch has no earlier removal to restore from. It makes the known public /downloads key exposure worse.

### 63. `183209f` Fix seat alert color and live_score not resetting to green after review
- **Intent:** Count only open (unacknowledged) alerts in the live tile score so a PC turns green after review. Fix the inverted ring colour logic, add a batch-acknowledge endpoint, and reconcile tile scores when the dashboard loads.
- **Touched:** `AlertController` (new `acknowledgeBatch`, `syncPcLiveState`), `DashboardController::index/pc`, `EventIngestor::refreshLiveScore/reconcileLiveScores/applyOpenAlertsToPc`, `Pc::resolveUiStatus`, `routes/web.php` (`POST /alerts/ack-batch`), `app.js`, `live.blade.php`, tests.
- **Problems:**
  - Scoring traceability: OK. Review does not delete alerts, reasons or evidence. Acknowledging sets `acknowledged_by/at/comment` and writes `Audit::log('alert.ack')`. Only the live tile (`live_score`, documented as "not the integrity score") ignores acknowledged alerts. Design gap: acknowledging is the only review action, so "reviewed and confirmed" and "reviewed and dismissed" both turn the tile green. Still present at HEAD.
  - [SEC] `acknowledgeBatch` (`AlertController.php:62`) accepts up to 500 arbitrary alert IDs. It has no lab or session scoping and no "already acknowledged" check. It re-stamps `acknowledged_by/at` and overwrites `comment` on alerts that were already reviewed, so the original reviewer and time are lost (the audit log keeps the history). The single-alert `acknowledge` has the same overwrite behaviour, which existed before. Still present at HEAD.
  - Behaviour change: with no active session, `refreshLiveScore` used to look back 2 hours. It now counts **every** unacknowledged alert ever raised for the PC (`EventIngestor.php:409`), so old unreviewed alerts keep a tile red indefinitely.
  - GET `/dashboard` and GET `/pcs/{pc}` now write to the database (`$pc->save()`) as a side effect.
  - The JS forces a group to `{status:'clean', score:0}` on the client when no visible alerts are left. This can hide open alerts that are not in the current list (from other sessions).
- **Undid / broke:** It changes the meaning of the ring colour: before, `score < 70` showed red ("health" style); now `score >= 50` shows red ("risk points" style). The old logic looks like the actual bug, so this is a fix.

### 64. `3c2abc1` Observe allowed AI/IDE proxy traffic, sync Windows helper PAC, and update proxy DNS
- **Intent:** Have the proxy emit `observed` events for allowed traffic to AI/IDE domains or paths, and turn them into server alerts. Have the Windows helper apply the CCC PAC URL to HKCU. Make the Windows SNI sniffer rebind when the IP changes. Pin DNS for the proxy container. Emit IDE-host AI flows that match no installed extension.
- **Touched:** proxy `xephyguard_addon.py`, `proxy_logic.py` (`match_ai_domain/path`, `should_emit_observed`); server `NetEventsController`, `DashboardController::pc`; agent `helper_main.cpp` (`set_user_proxy_pac`), `agent.cpp` (`classify_ai_flow`, unclaimed-flow emit, `proxy_pac_url` in helper config, `clear_proxy_pac` on shutdown), `network_win.cpp`; compose files; tests. Crosses four components (proxy, server, agent, ccc-deploy) in one commit.
- **Problems:**
  - [PRIV][INV] The proxy now logs host **and path** (query removed, max 256 chars) for every allowed AI/IDE request. It does this even when `log_urls` is disabled (`xephyguard_addon.py:74,117`), and in every mode, including with no session. `docs/ETHICS.md:22` says "Visited hosts/paths of allowed sites — off by default". Paths such as `/c/<conversation-id>` identify individual conversations. No request bodies, headers or prompts are logged (verified in the diff). ETHICS.md was not updated. Still present at HEAD.
  - [INV] Every `observed` event creates a `severity: high` alert (`NetEventsController.php:85`), even though the traffic was **allowed** by the active policy (AI-permitted mode, exempt group, or no session). The alert carries host, matched rule and layer, so it can be traced. But permitted behaviour turns the tile red at high severity, which is close to an automatic penalty. Rule id and severity are hard-coded in the controller. Still present at HEAD.
  - [INV] New hard-coded AI domains in agent core: `copilot-proxy.githubusercontent.com`, `copilot-telemetry.githubusercontent.com`, `copilot.microsoft.com` in `flow_matches_ext` (`agent.cpp:614-616`). The same domains are already in `data/blocklists/ide_ai_backends.txt`. The IDE/browser exe lists were moved into `is_extension_host_proc`/`is_browser_proc`, which makes the known hard-coded lists more permanent. Still present at HEAD.
  - [SEC] The PAC URL comes from CCC policy and is not validated (no scheme or host check) before it is written into `HKCU\...\AutoConfigURL`. By design it is `http://<ccc>/proxy.pac` (`policy.hpp:53`). A PAC file over plain HTTP can be replaced by anyone on the lab network, which redirects browser traffic. That is not an agent↔CCC mTLS channel, but THREAT_MODEL.md was not updated.
  - Race in `network_win.cpp`: the sniffer thread copies `sni_sock_` under the lock and then calls `recv` outside it, while `stop()`/rebind can `closesocket` the same handle at the same time. Windows code, compile-checked only.
  - [HACK] `dns: [127.0.0.1, 8.8.8.8]` is hard-coded on the proxy service in both compose files (`docker-compose.yml:77`). It sends lookups to Google as a fallback and is a fixed external dependency. With `network_mode: host`, Docker may ignore or reject `dns:` (unverified).
  - No PROTOCOL.md update for the new `observed` action or the `proxy_pac_url`/`clear_proxy_pac` helper IPC fields.

### 65. `4ac2d8e` Implement Linux audit fixes, X11 live screen fixes, and non-overlapping status indicator
- **Intent:** A very large commit: 84 files, about 3.8k lines added. It adds a Linux live screen stream from the user-level helper over a new WebSocket client, a "viewer is watching" indicator, a repositioned X11 status pill, Linux browser policy and PAC/DNS lock (Chromium/Firefox managed policies, systemd-resolved drop-in), an AF_PACKET SNI sniffer and socket-diag byte counters, Intel GPU stats, **server-side screenshot "evidence" storage**, heartbeat-timeout changes, installer/systemd changes, and screen gateway status messages.
- **Touched:** agent (helper_main, screen_streamer, websocket, tls, util, config, Linux adapters, Windows adapters, tests), xephyguard-screen (x11_capture, encoder, relay.go), server (new `EvidenceController`, `EvidenceCapture` model and migration, reports, PurgeRetention, Bootstrap, EventIngestor, AgentHealthClassifier, app.js), ccc-deploy, installers, proxy `generate.py`, docs/SETUP_CCC.md. Almost every component in one commit.
- **Problems:**
  - [PRIV][INV] New feature: instructors (and assistants, since the route is in the `role:assistant,instructor` group) can save screenshots of a student's screen to the server. Route: `POST /pcs/{pc}/evidence` → `storage/app/private/evidence/...`. The screenshots are kept for the raw-data retention period and listed in per-student session reports. `docs/ETHICS.md:24` says "Never collected: … screenshots or screen content", and the CLAUDE.md invariant forbids storing screen content. The commit subject does not mention this feature, and ETHICS.md / FEATURES.md / DECISIONS.md were not updated. The student is not notified when a screenshot is saved. Before this, the "evidence" button was a fake toast. Still present at HEAD (`EvidenceController.php:19`, `routes/web.php` `pcs.evidence`/`evidence.show`).
  - [SEC] The evidence upload is a browser-side `canvas.toBlob` sent by staff. Nothing ties the image to the PC's real stream, so any staff member can upload any image as "evidence" for any PC. The SHA-256 is computed only at upload time. `store()` picks the session with `LabSession::where('status','active')->first()`, which ignores the lab. `show()` has no per-lab check.
  - [SEC][INV] A plaintext WebSocket mode was added to the agent core `WebSocketClient` (`use_tls=false` → raw `tcp_connect`, `websocket.cpp:185`). The Linux helper uses it for the screen stream with defaults `screen_tls=false`, port 8082 (`config.hpp:14-16`, `helper_main.cpp:531`). Live **screen content** and the `X-Agent-Token` therefore travel unencrypted on the lab network by default. The stream is also on by default (`screen_enabled = true` on both platforms, `kDefaultScreenEnabled = true` in the helper). This is not the agent↔CCC :8443 channel, but it is a new non-TLS path in agent core. Still present at HEAD.
  - [SEC] The Linux install command on /downloads is now forced to `http://` (`$httpBaseUrl`) for `curl … | sudo bash` (`downloads.blade.php:19`). The root install script is fetched over plaintext even when the page itself is HTTPS. Still present at HEAD.
  - [PRIV] On boot, `Bootstrap` silently adds `files: true, artifacts: true` to the collection settings of existing built-in profiles (`Bootstrap.php:49`). That widens data collection without an admin action. `EventIngestor` also stops sending `local_model`/`file.*` hits to the Installed tab and raises them as alerts.
  - [INV] More hard-coded signatures outside data/: `is_ai_telemetry_domain` in agent core (`util.cpp:176`: `telemetry`, `exp-tas.com`, `events.data.microsoft.com`, …); `fallback_ai_domain_for_proc` in the Linux adapter (`network_monitor.cpp:77`: `claude`→`api.anthropic.com`, `ollama`→`ollama.com`, copilot binaries); new `antigravity` IDE handling in `agent.cpp:432,539,579-594` and `rules.cpp:89`. This adds to the known hard-coded `ide_is_running` lists. Still present at HEAD.
  - Shell use: OK. New process launches use `fork`+`execlp` with argument vectors (`gsettings`, `systemctl`, and `Xvfb` in tests only). No `system()`/`popen()` was added. `dns_ip` is checked with `inet_pton` before it goes into `resolved.conf.d`. The PAC URL goes into browser policy JSON through nlohmann, not string concatenation.
  - Buffers: OK as far as reviewed. The X11 capture now copies row by row using `bytes_per_line` and handles a resize by returning `Lost`. `parse_tls_sni` and the AF_PACKET IPv4/IPv6 header parsing are bounds-checked before each read. The `_NET_WORKAREA` read checks the desktop index against `n`. Screen capture runs in the user-level helper with the user's own `DISPLAY` and does not need root `XAUTHORITY`. Not run.
  - Functional limit (unverified): the BPF filter keeps only the first 2 KB of the first outbound :443 segment. Chrome randomises the ClientHello extension order, and post-quantum ClientHellos span more than one segment, so SNI may often be missed.
  - Indicator: the helper shows "Monitored by XephyGuard · instructor is viewing your screen" while a viewer is attached. That is good, but the text **replaces** the base text, so mode and "network isolated" are hidden while someone is viewing. The X11 pill no longer uses mode colours (red for Test). If the indicator text is empty after sanitising, the pill is unmapped. The agent always sends non-empty text (`agent.cpp:883-886`), so the pill should normally be visible. Not run.
  - `policy.cpp` `work_dirs`: `d.get<std::string>()` throws on a non-string element sent by CCC. Whether that exception is caught is unverified.
  - Process: one commit mixes about ten unrelated features across every component, including a new data type (screenshots), with no PROTOCOL.md (new `screen_caps` IPC and gateway Status message), ETHICS.md or THREAT_MODEL.md update. The heartbeat timeout default changes from 15 to 45 s.
- **Undid / broke:** Removed the always-on X11 badge mapping in the constructor (it now maps on the first `set_indicator`) and the mode-coloured background. Changed the earlier suppression of `local_model`/`file.*` alerts.

### Batch 7 summary
1. [PRIV][INV] Commit 65 adds server-side storage of student screenshots ("evidence") that assistants and instructors can save. This is a direct breach of the ETHICS.md "never screenshots or screen content" rule, it is not mentioned in the commit subject, and the docs were not updated. Present at HEAD.
2. [SEC][INV] Commit 65 adds a plaintext WebSocket path to agent core. The Linux screen stream uses it by default (`screen_tls=false`, :8082), so screen content and the agent token travel unencrypted. Present at HEAD.
3. [SEC] Commits 61/62 show the lab enrollment key prominently on the unauthenticated /downloads page, and a test enforces it. Anyone can submit enrollments, and with MAC auto-approve a spoofed agent can be approved with no human. Commit 65 also forces `curl http://… | sudo bash`. Present at HEAD.
4. [PRIV][INV] Commit 64 always logs host and path of allowed AI/IDE requests, ignoring the `log_urls` off-by-default rule. Each one raises a high-severity alert even when AI is allowed. Present at HEAD.
5. [INV] Commits 64/65 add more hard-coded AI domains, processes and IDEs in agent core and the Linux adapter. Present at HEAD.
6. [PRIV] Commit 65 silently turns on `files`/`artifacts` collection in existing built-in profiles at boot. Present at HEAD.

---

## Overall summary — all 65 commits

Items marked ✔ were re-checked directly against HEAD (`4ac2d8e`) while merging, not only taken
from the batch notes. Everything listed is **still present at HEAD** unless stated.

### Security
1. ✔ **Public `/share` text pad** (`0290fc7`, grown in `3bcde4a`/`a811978`): no login, anyone on the
   network can read/post, keeps the last 20 texts, posts are labelled "by Instructor". Works as a
   message board between exam seats. `server/routes/web.php:27-30`.
2. ✔ **Secret enrollment key on the public `/downloads` page** (`6e94215`/`e9ded61`):
   `PublicController::downloadsIndex` passes `$lab->enrollKey()` to guests. With MAC auto-approve,
   a spoofed MAC can get an agent cert signed without a human. Linux install is
   `curl http://… | sudo bash`.
3. ✔ **Screen stream bypasses mTLS** (WP2 `14b0a11` onward): gateway on plain `ws://` :8082,
   `screen_tls = false` by default (`agent/core/include/xg/config.hpp:16`), WebSocket Origin
   check off (`relay.go:179,369`), trusts a client-set `X-Agent-Cert-Fingerprint` header
   (`auth.go:130`), one screen token shared by all PCs.
4. ✔ **Any logged-in user can watch any idle PC, and pick another PC's stream**
   (`ViewTokenController::issuePcToken`): ownership is only checked when the PC is in an active
   session, and the signed `stream_id` comes from the request.
5. ✔ **Students can read the PC's mTLS private key** (`4fd1a79`): `icacls … *S-1-5-32-545:(OI)(CI)RX`
   on `ProgramData\XephyGuard` (`installers/windows/xephyguard.nsi:196`).
6. ✔ **Self-signed code-signing cert installed into LocalMachine\Root and TrustedPublisher on every
   PC** (`7cd93b2`, `xephyguard.nsi:207-210`); `.cer` files tracked in `branding/`. The PFX
   password was committed in `7cd93b2` and removed in `db50a52`, but is still in history: rotate it.
7. ✔ **`gateway.exe` (10.5 MB) still tracked** although `.gitignore` lists it; built before
   `db50a52`, so it contains the old hard-coded dev JWT secret and does not match the source.
8. ✔ **Open proxy on all interfaces** (`de47069`): mitmproxy on `0.0.0.0:8080/8085` with
   `block_global=false`; the proxy container holds the root CA key that also signs agent certs.
9. ✔ **Signatures auto-published every minute** (`7216fa3`, `server/routes/console.php:11-20`) with
   no review step: any rule edit reaches every agent within a minute.
10. ✔ X11 shared-memory frame buffer created with mode `0777` (`x11_capture.cpp:44`): any local
    user can read the screen image.
11. Audit log failures are swallowed (`Audit::log()` catches everything, `dda63f7`).
12. Thread-safety: helper WebSocket client used by 2–3 threads without a lock; snapshot thread
    never joined (`std::terminate` when a view starts after a snapshot).
13. ✔ Dead `insecure_skip_verify` TLS option (initial commit). Root `popen` for `nft` in the Linux
    isolator: constant command strings, data via stdin, so low injection risk, but it still runs
    a shell as root.

### Privacy / ethics (conflicts with docs/ETHICS.md)
1. ✔ **Live screen streaming** (WP0–WP6 and later) while `docs/ETHICS.md:24` says screenshots and
   screen content are never collected. ETHICS, DECISIONS, PROTOCOL and THREAT_MODEL were never
   updated. `PrivacyFilter` is not wired in; exam mode turns masking off.
2. ✔ **Screenshots stored on the server as "evidence"** (`4ac2d8e`, `POST /pcs/{pc}/evidence`).
   Not in the commit subject, no docs, student not notified.
3. Full window titles, clipboard metrics and keystroke timing turned on in every profile,
   including Always-on (`f599f4d`, `00d697f`); raw titles skip `filter_title`; file names leak
   into copy/paste events. `4ac2d8e` also turns on `files`/`artifacts` in existing profiles at boot.
4. ✔ Raw packet capture as SYSTEM on Windows (`SIO_RCVALL`, `network_win.cpp:535`); HKLM browser
   DNS policies changed silently.
5. Allowed AI/IDE traffic is always logged (host + path) even with `log_urls` off, and raises
   high-severity alerts while AI is allowed (`3c2abc1`).
6. The student notice (`5269f30`) dropped disclosures and says screens are only viewed in tests and
   never recorded, which HEAD contradicts.
7. `04a0d3b` deleted appeals, student portal, two-person gate, ethics pack, fairness analysis and
   the ethics contract tests; none came back.
8. HTML session report lost traceability (`53e7d8e`): no signal IDs, review comments or
   "not observable" column, a different "integrity" score from PDF/CSV, a hard-coded `+5`.

### Rule violations and behaviour risks
- Hard-coded detection lists in agent core and Linux adapter (`0a483a2`, `3c2abc1`, `4ac2d8e`).
- Kill is the default for every high/critical hit in test mode (`f7b0d77`); broad exe names
  (`aide`, `comet`, `copilot`) can close unrelated programs.
- False "terminated by user" tamper flag on every helper disconnect, including logoff
  (`31453be`); Task Manager stays disabled machine-wide if the agent crashes.
- CI: Windows tests off (`964b4a7`); unsigned installer, version silently falls back to 0.1.0,
  unpinned NSIS/pyyaml (`80bfcbf`/`686ff42`).
- Protocol changes (`rescan`, new policy fields, screen channel) without `docs/PROTOCOL.md`;
  `docs/FEATURES.md` and `docs/PLAN.md` referenced by CLAUDE.md do not exist.
- Process: many commits mix components, features hidden under "tests"/"fix" titles, a revert
  mixed with new features, a static-demo UI phase (fake login, fake data) partly left behind
  (`public/js/data.js` still served).

### Suggested order of work (for discussion, nothing changed yet)
1. Lock down or remove `/share`; remove the enrollment key from `/downloads`; fix the ProgramData
   ACL; rotate the code-signing cert/password and stop installing it into Root.
2. Decide the screen-streaming question with an ETHICS.md / DECISIONS.md update first; then put
   the screen channel behind TLS + per-PC auth, bind `stream_id` to the PC, fix `issuePcToken`.
3. Untrack `gateway.exe`; restrict the proxy bind; add review before signature publish.
4. Move hard-coded lists into `data/`; restore traceability in reports; document protocol changes.
