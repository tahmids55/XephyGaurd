# XephyGuard — feature inventory

Every feature that exists in the code at commit `4ac2d8e` (branch `taki`), numbered so we can refer
to them (F1, F2, …). Built from the current code and `docs/HISTORY.md`. No code was changed.

**How the status was decided**
- **working**: an automated test for it passed on 2026-11-17 (agent 41/41, server 66/66, proxy 15
  + 2 skipped, analysis 7, eval 4). Not clicked through by hand.
- **partly working**: works, but has a known gap or defect (see `docs/HISTORY.md`).
- **broken**: confirmed not doing what it says.
- **unclear**: no automated test and not run by hand, or Windows-only (compile-checked only, never
  run on real Windows).

Flags: ⚠SEC security problem, ⚠PRIV conflicts with `docs/ETHICS.md`. These do not change the
status: a feature can work and still be a problem.

Roles: `assistant` < `instructor` < `admin`. `admin` passes every role check
(`User::hasRole`, `server/app/Models/User.php:30`). Route groups: `role:assistant,instructor`,
`role:instructor`, `role:admin` in `server/routes/web.php`.

Paths are relative to the repo root. `S/` = `server/app/`, `A/` = `agent/`.

---

## Contents
1. [CCC setup and operations](#1-ccc-setup-and-operations) — F1–F9
2. [Staff accounts and access](#2-staff-accounts-and-access) — F10–F13
3. [Enrollment and PC management](#3-enrollment-and-pc-management) — F14–F22
4. [Live lab dashboard](#4-live-lab-dashboard) — F23–F26
5. [Sessions, profiles and timetable](#5-sessions-profiles-and-timetable) — F27–F33
6. [Commands to PCs](#6-commands-to-pcs) — F34–F38
7. [Alerts and review](#7-alerts-and-review) — F39–F42
8. [Screen viewing and evidence](#8-screen-viewing-and-evidence) — F43–F47
9. [Scoring, reports and research](#9-scoring-reports-and-research) — F48–F53
10. [Roster and consent](#10-roster-and-consent) — F54–F57
11. [Signatures and blocklists](#11-signatures-and-blocklists) — F58–F63
12. [Network enforcement (gateway, DNS, proxy)](#12-network-enforcement-gateway-dns-proxy) — F64–F71
13. [Agent: monitoring on lab PCs](#13-agent-monitoring-on-lab-pcs) — F72–F86
14. [Agent: enforcement on lab PCs](#14-agent-enforcement-on-lab-pcs) — F87–F92
15. [Session helper (student desktop)](#15-session-helper-student-desktop) — F93–F97
16. [Public pages](#16-public-pages) — F98–F102
17. [Admin: health, settings, audit](#17-admin-health-settings-audit) — F103–F106
18. [Background jobs](#18-background-jobs) — F107–F111
19. [Packaging and deployment tools](#19-packaging-and-deployment-tools) — F112–F118
20. [Evaluation tools](#20-evaluation-tools) — F119–F122
- [Environment variables](#environment-variables)
- [Config options](#config-options)
- [Third-party integrations](#third-party-integrations)
- [Database tables](#database-tables)

---

## 1. CCC setup and operations

### F1. First-run setup wizard
- **What:** a web form that creates the first admin account and the lab (name, CCC IP, network
  mode) on a fresh server.
- **Trigger:** `GET /setup`, `POST /setup` (throttled 10/min). Available until an admin exists.
- **Code:** `S/Http/Controllers/Web/SetupController.php` (`show`, `store`), `S/Services/Bootstrap.php`.
- **In/out/side effects:** writes `users`, `labs`, `settings`; regenerates network state
  (`NetworkPolicyWriter::write` → `storage/app/net/state.json`); audit entry.
- **Status:** working (`first_run_wizard_then_login`).
- **Test:** 1. Fresh DB, open `/setup`. 2. Fill the form. 3. You are sent to `/login` and can log in.

### F2. Bootstrap command
- **What:** seeds the lab, the 5 built-in profiles, blocklists and rules from `data/`, publishes
  signatures and writes network state.
- **Trigger:** `php artisan xephyguard:bootstrap`.
- **Code:** `S/Console/Commands/BootstrapCommand.php`, `S/Services/Bootstrap.php::run`,
  `BlocklistService::importSeed`, `SignatureCompiler::importSeed/publish`.
- **In/out/side effects:** reads `data/blocklists/*.txt`, `data/signatures/*.yaml`; writes `labs`,
  `profiles`, `blocklist_entries`, `rules`, `signature_versions`; writes `state.json`.
- **Status:** working (`bootstrap_publishes_bundle_with_ai_and_ide_domains`). Seed import inserts missing
  rules and updates the others from the YAML, except rules saved in the dashboard (`rules.edited`), which
  it never overwrites (S-49, `SignatureSeedImportTest`).
- **Test:** 1. `cd server && php artisan xephyguard:bootstrap`. 2. `/profiles` lists 5 profiles;
  `/signatures` shows an active version.

### F3. Internal certificate authority
- **What:** creates the lab CA, issues the CCC server certificate (SANs = IPs + DNS names) and
  later signs agent client certificates and signature bundles.
- **Trigger:** `php artisan xephyguard:ca:init [--cn=] [--server-ip=*] [--server-dns=*] [--force]`;
  used automatically by enrollment (F15) and publishing (F61).
- **Code:** `S/Console/Commands/CaInit.php`, `S/Services/CertificateAuthority.php`
  (`create`, `issueServerCert`, `signClientCsr`, `signData`, `verifyData`, `fingerprint`).
- **In/out/side effects:** writes the CA key and cert to `XG_CA_DIR` (default
  `storage/app/private/ca`), server cert/key files.
- **Status:** working (used by the enrollment and signature tests). ⚠SEC the same CA key also
  signs bundles and sits in the proxy container (`docs/HISTORY.md`, batch 5).
- **Test:** 1. `php artisan xephyguard:ca:init --server-ip=127.0.0.1`. 2. `curl -s localhost:8000/ca.crt | openssl x509 -noout -subject -fingerprint -sha256`.

### F4. CCC doctor (self-check)
- **What:** checks the CCC (DB, CA, Reverb, network state, services) and explains problems in plain
  language.
- **Trigger:** `php artisan xephyguard:doctor [--json]`; `xephyguard-ccc doctor` (F113); shown on the
  Health page (F103).
- **Code:** `S/Console/Commands/DoctorCommand.php`, `S/Services/Doctor.php::run`.
- **In/out/side effects:** read-only checks; prints a list or JSON.
- **Status:** unclear (no test).
- **Test:** 1. `php artisan xephyguard:doctor`. 2. Stop Reverb and run it again; it should report it.

### F5. Network state writer
- **What:** regenerates the JSON state the DNS, nftables and proxy containers read (groups, PC
  IPs, blocklists, inspect hosts, proxy mode).
- **Trigger:** `php artisan xephyguard:net:write`; automatically after saving settings, profiles,
  blocklists, retiring a PC, setup.
- **Code:** `S/Console/Commands/NetWrite.php`, `S/Services/NetworkPolicyWriter.php` (`build`, `write`).
- **In/out/side effects:** writes `XG_NET_STATE` (default `storage/app/net/state.json`).
- **Status:** working (`blocklist_edit_hot_reloads_network_state`,
  `always_on_udm14_populates_udm14_clients_and_saving_profile_writes_net_state`).
- **Test:** 1. `php artisan xephyguard:net:write`. 2. `jq . storage/app/net/state.json | head`.

### F6. Enrollment key command
- **What:** shows or rotates the lab enrollment key from the shell.
- **Trigger:** `php artisan xephyguard:enroll-key [--rotate]`; `xephyguard-ccc enroll-key`.
- **Code:** `S/Console/Commands/EnrollKey.php`, `S/Models/Lab.php::enrollKey`.
- **In/out/side effects:** `--rotate` updates the lab's key (old key stops working).
- **Status:** working (`key_rotation_invalidates_old_key` covers rotation).
- **Test:** 1. Run it twice with `--rotate`. 2. An agent enrolling with the old key is rejected.

### F7. Signature publish command
- **What:** compiles, signs and activates the signature bundle from the DB (optionally importing
  new rules from `data/signatures` first).
- **Trigger:** `php artisan xephyguard:signatures:publish [--import]`.
- **Code:** `S/Console/Commands/PublishSignatures.php`, `S/Services/SignatureCompiler.php`.
- **In/out/side effects:** writes `signature_versions`; agents fetch it with their policy.
- **Status:** working (`signature_publish_and_rollback`, `publish_is_noop_when_nothing_changed`).
- **Test:** 1. Run it. 2. `/signatures` shows a new active version (or "no change").

### F8. Discovery service (UDP)
- **What:** lets agents find the CCC on the LAN without a configured host.
- **Trigger:** UDP broadcast to port `XG_DISCOVERY_PORT` (47474); Docker service `discovery`.
- **Code:** `ccc-deploy/services/discovery.py`; agent side `A/core/src/discovery*` (reply parsing).
- **In/out/side effects:** replies with CCC address and CA fingerprint; no writes.
- **Status:** partly working (agent reply parsing tested; the service itself has no test).
- **Test:** 1. Start `python3 ccc-deploy/services/discovery.py`. 2. From a lab PC, run the agent
  enroll without `--ccc` and check it finds the server.

### F9. Analysis scoring service (HTTP)
- **What:** separate Python service the CCC calls to score a session.
- **Trigger:** `python -m xg_analysis serve` (Docker service `analysis`, 127.0.0.1:8090);
  `GET /health`, `POST /score`.
- **Code:** `analysis/xg_analysis/service.py`, `pipeline.py`, `scoring.py`, `features.py`, `db.py`;
  CCC side `S/Services/AnalysisClient.php`.
- **In/out/side effects:** reads events from the DB (`XG_DB_URL`); returns scores and reasons.
- **Status:** working (analysis tests; `ending_a_session_survives_an_unreachable_analysis_service`).
- **Test:** 1. `cd analysis && python -m xg_analysis serve`. 2. `curl localhost:8090/health`.

---

## 2. Staff accounts and access

### F10. Login and logout
- **What:** staff sign in with email and password; logout ends the session.
- **Trigger:** `GET /login` (+ `/login.html`), `POST /login`, `POST /logout`.
- **Code:** `S/Http/Controllers/Web/AuthController.php` (`show`, `login`, `logout`).
- **In/out/side effects:** Laravel session; sets `users.last_login_at`; audit entry.
- **Status:** working (`first_run_wizard_then_login`).
- **Test:** 1. Open `/login`. 2. Log in. 3. Wrong password shows an error.

### F11. Role-based access
- **What:** pages and actions are limited by role; admins can do everything.
- **Trigger:** every route in the `role:` groups; nav items hidden by role.
- **Code:** `S/Http/Middleware/RequireRole.php`, `User::hasRole`, `routes/web.php`.
- **Status:** working (`roles_are_enforced_server_side`).
- **Test:** 1. Log in as an assistant. 2. Open `/settings`: expect 403.

### F12. Staff accounts management
- **What:** admin lists, creates and edits staff accounts (name, email, role, active).
- **Trigger:** `GET /users` (+ `.html` aliases), `POST /users/{user?}`. Admin only.
- **Code:** `S/Http/Controllers/Web/AdminController.php` (`users`, `saveUser`).
- **In/out/side effects:** writes `users`; audit entry.
- **Status:** partly working (renders in `every_screen_renders_for_admin`; create/edit not tested).
- **Test:** 1. `/users` → add an assistant. 2. Log in as that user.

### F13. Dashboard live channel auth
- **What:** dashboard users subscribe to the lab's live channel; nobody but the agent may join a
  PC channel.
- **Trigger:** Reverb private channel `lab.{labId}`; `pc.{pcId}` always denied for users.
- **Code:** `server/routes/channels.php`.
- **Status:** working (`broadcast_auth_only_for_own_channel` covers the agent side).
- **Test:** 1. Open the dashboard with dev tools. 2. The WebSocket subscribes to `private-lab.1`.

---

## 3. Enrollment and PC management

### F14. Agent enrollment request
- **What:** a lab PC asks to join the lab with the enrollment key and a CSR.
- **Trigger:** agent `xephyguard-agent enroll --key <key> [--ccc host] [--ca-fingerprint] [--trust-on-first-use] [--no-wait] [--timeout]`
  → `POST /api/agent/v1/enroll` (30/min), then polls `GET /api/agent/v1/enroll/{enrollment}` (120/min).
- **Code:** agent `A/apps/agent_main.cpp`, `A/core/src/enroll.cpp`; server
  `S/Http/Controllers/Agent/EnrollController.php`, `S/Services/EnrollmentService.php` (`register`, `poll`).
- **In/out/side effects:** writes `enrollments` (hostname, MACs, machine_uid, OS, CSR, IP);
  supersedes older pending requests for the same host. On approval the agent receives its cert,
  token and CA and stores them in its data dir.
- **Status:** working (`approve_dont_configure_flow`, `enroll_api_rejects_malformed_machine_uid`).
- **Test:** 1. On a PC: `sudo xephyguard-agent enroll --key <key> --ccc <ip>`. 2. It appears on `/enroll`.

### F15. Approve or reject enrollments
- **What:** staff approve (optionally naming the PC) or reject pending PCs; "approve all".
- **Trigger:** `GET /enroll` (+ `.html`), `POST /enroll/{enrollment}/approve`,
  `POST /enroll/approve-all`, `POST /enroll/{enrollment}/reject`. Assistant and up.
- **Code:** `S/Http/Controllers/Web/EnrollmentController.php`, `EnrollmentService::approve/reject`.
- **In/out/side effects:** signs the client cert (F3); creates or re-uses a `pcs` row (matched by
  machine_uid / MAC / hostname); revokes old `agents` rows and creates a new one with a token.
- **Status:** working (`auto_approve_by_mac_and_reenrollment_keeps_the_pc`,
  `cloned_machines_with_same_hostname_and_mac_stay_separate_pcs`,
  `legacy_pc_is_adopted_once_only_when_hostname_and_mac_match_uniquely`).
- **Test:** 1. Enroll a PC (F14). 2. Approve on `/enroll`. 3. The agent finishes enrolling and the PC shows on the dashboard.

### F16. Auto-approve by MAC (REMOVED in S-16, 2026-11-18)
- **Now:** route, controller action and UI are gone; `labs.auto_approve*` columns are ignored. Replaced by
  one-time per-PC enrollment tokens (S-14). Test: `known_mac_never_auto_approves_and_reenrollment_keeps_the_pc`.
- **What:** admin turns on automatic approval and pastes a list of allowed MAC addresses; matching
  PCs are approved without a click.
- **Trigger:** `POST /enroll/auto-approve` (`auto_approve`, `macs`). Admin only.
- **Code:** `EnrollmentController::autoApprove`, `EnrollmentService`.
- **In/out/side effects:** writes `labs.auto_approve` and `labs.auto_approve_macs`; audit entry.
- **Status:** working (`auto_approve_by_mac_and_reenrollment_keeps_the_pc`). ⚠SEC with the key public
  (F99), a spoofed MAC gets a cert without a human.
- **Test:** 1. Turn it on. 2. Re-enroll a known PC: it is approved without clicking.
- **Planned:** removed (DECISIONS #28, docs/plan/server-implementation.md S-16). Approval comes only from a
  one-time per-PC token or a staff click.

### F17. Rotate enrollment key (web)
- **What:** admin replaces the enrollment key.
- **Trigger:** `POST /enroll/rotate-key`. Admin only.
- **Code:** `EnrollmentController::rotateKey`.
- **Status:** working (`key_rotation_invalidates_old_key`).
- **Test:** 1. Rotate. 2. Enrolling with the old key fails.

### F18. Rename a PC
- **Trigger:** `POST /pcs/{pc}/rename`. Assistant and up.
- **Code:** `EnrollmentController::rename`. Writes `pcs.name`; audit.
- **Status:** unclear (no test).
- **Test:** 1. Rename a PC from the inspector or `/enroll`. 2. The new name shows on the seat map.

### F19. Retire a PC
- **What:** removes a PC from the lab; its agent is refused afterwards.
- **Trigger:** `POST /pcs/{pc}/retire`. Admin only.
- **Code:** `EnrollmentController::retire`. Sets `pcs.retired_at`, revokes agents, rewrites net state; audit.
- **Status:** working (`revoked_and_retired_agents_are_refused`).
- **Test:** 1. Retire a PC. 2. Its agent's next request gets 403.

### F20. Agent authentication (mTLS + token)
- **What:** every agent API call must carry a valid client cert (checked by nginx, passed as
  headers) and the agent's bearer token.
- **Trigger:** middleware `agent` on `/api/agent/v1/policy|events|broadcasting/auth`.
- **Code:** `S/Http/Middleware/AuthenticateAgent.php`; nginx `ccc-deploy/nginx/xephyguard.conf` (:8443).
- **Status:** working (`mtls_and_token_are_both_required`). Always enforced; `XG_REQUIRE_MTLS` was removed (S-10).
- **Test:** 1. `curl -k https://<ccc>:8443/api/agent/v1/policy` without a cert: expect 4xx.

### F21. Agent channel auth (Reverb)
- **What:** an agent may subscribe only to its own `private-pc.<id>` command channel.
- **Trigger:** `POST /api/agent/v1/broadcasting/auth`.
- **Code:** `S/Http/Controllers/Agent/AgentController.php::broadcastAuth`.
- **Status:** working (`broadcast_auth_only_for_own_channel`).
- **Test:** covered by the test; by hand, request another PC's channel and expect 403.

### F22. Seat layout editor
- **What:** staff arrange PCs on the seat map and save the layout.
- **Trigger:** `POST /lab/layout` from the Live lab page. Assistant and up.
- **Code:** `DashboardController::saveLayout`; writes/deletes `seats`; audit.
- **Status:** unclear (no test).
- **Test:** 1. Drag a PC to another seat and save. 2. Reload: it stays there.

---

## 4. Live lab dashboard

### F23. Live lab seat map
- **What:** grid of seats with online/offline status, colour by open alerts and live score,
  updated live over Reverb.
- **Trigger:** `GET /` (+ `/index.html`). Assistant and up. Events `PcUpdated`, `AlertRaised`,
  `SessionChanged` on `lab.{id}`.
- **Code:** `S/Http/Controllers/Web/DashboardController.php::index`, `resources/views/dashboard/live.blade.php`,
  `resources/js/app.js`, `S/Events/*`.
- **Status:** working (`dashboard_includes_all_52_unacknowledged_alerts_including_retired_pcs`,
  `dashboard_reconciles_stale_live_score_when_no_open_alerts_exist`).
- **Test:** 1. Open `/` with one PC online. 2. Stop the agent: the seat goes offline within ~15 s.

### F24. PC inspector (9 tabs)
- **What:** per-PC detail: processes, extensions/installed AI, network/ports/adapters, USB devices,
  copy/paste, browser tabs, events timeline, screen, alerts.
- **Trigger:** `GET /pcs/{pc}` (JSON for the drawer). Assistant and up.
- **Code:** `DashboardController::pc` (also marks some data as seen).
- **Status:** working (`copy_from_chrome_and_paste_in_vscode_updates_dashboard`,
  `every_screen_renders_for_admin`). ⚠PRIV shows window/tab titles and file names (F75, F79).
- **Test:** 1. Click a seat. 2. Open each tab and check it has data.

### F25. Health classification of PCs
- **What:** labels a silent PC as `AGENT_UNAVAILABLE` or `SUSPECTED_TAMPERING` by comparing with its
  neighbours.
- **Trigger:** used by the dashboard and machine-silent detection (F108).
- **Code:** `S/Services/AgentHealthClassifier.php::classify`.
- **Status:** working (11 unit tests pass; `assess()` returns the label with reasons; `detectSilent` stores both in the alert details).
- **Test:** 1. Stop the agent on one PC while others run. 2. After 3 missed heartbeats it is marked suspected tampering.

### F26. Synthetic desktop drawer (demo)
- **What:** fake student desktops shown in the UI when no real stream exists.
- **Trigger:** Live lab page; gated off in `4ac2d8e`.
- **Code:** `server/resources/js/app.js`; `server/resources/demo/data.js` (demo data), `PublicController::demoData`.
- **Status:** unclear (leftover demo code; `docs/HISTORY.md` batch 6). The dataset is no longer in `public/`; `GET /js/data.js` serves it only when `APP_ENV=local` (404 elsewhere).
- **Test:** 1. `curl -si localhost:8000/js/data.js | head -1`: 404 unless `APP_ENV=local`.

---

## 5. Sessions, profiles and timetable

### F27. Start, stop and switch a session
- **What:** staff start a lab session with a profile (Practice/Test/Exam), stop it, or switch the
  profile mid-session. All PCs get the new policy within seconds.
- **Trigger:** `GET /sessions` (+ `.html`), `POST /sessions`, `POST /sessions/{session}/stop`,
  `POST /sessions/{session}/profile`. Assistant and up.
- **Code:** `S/Http/Controllers/Web/SessionController.php`, `S/Services/SessionManager.php`
  (`start`, `stop`, `switchProfile`), `PolicyService`.
- **In/out/side effects:** writes `lab_sessions`, `session_pcs`; sends `policy_updated` to PCs;
  writes net state; stopping queues scoring (F48).
- **Status:** working (`test_session_blocks_and_ending_relaxes`, `practice_block_profile_uses_block_ai_group`).
- **Test:** 1. Start "Test: AI blocked". 2. On a lab PC `chatgpt.com` is blocked. 3. Stop: it loads again.

### F28. Per-PC profile override
- **What:** instructor gives one PC a different profile inside a session, with a reason.
- **Trigger:** `POST /sessions/{session}/pcs/{pc}/override`. Instructor and up.
- **Code:** `SessionController::override`, `SessionManager::setOverride`.
- **Status:** unclear (no test).
- **Test:** 1. In a Test session, override one PC to Practice. 2. AI works on that PC only.

### F29. Profiles
- **What:** list of profiles; admin creates, edits and deletes them (mode, collectors, intervals,
  actions, title capture, proxy options, notice text).
- **Trigger:** `GET /profiles` (+ `.html`, instructor and up); `POST /profiles/{profile?}`,
  `DELETE /profiles/{profile}` (admin).
- **Code:** `S/Http/Controllers/Web/ProfileController.php`, `S/Models/Profile.php`, `PolicyService`.
- **In/out/side effects:** writes `profiles`; rewrites net state; audit.
- **Status:** partly working (saving tested by the udm14 test; 5 built-ins exist; the `google_udm14`
  default flipped to off, `docs/HISTORY.md` batch 5).
- **Test:** 1. Edit a profile's scan interval. 2. A PC in that profile gets the new interval (agent log).

### F30. Built-in profiles
- **What:** Always-on (no session), Practice: AI allowed + logged, Practice: AI blocked,
  Test: AI blocked, Exam: strict.
- **Code:** `S/Services/Bootstrap.php:15-27`.
- **Status:** working; matches ETHICS.md since S-21: Always-on has window/clipboard/keys/files off and
  titles off; Practice has files off and titles `app_only`; Test `app_only`, Exam `full` (capped by the
  Test title setting). Bootstrap clamps existing built-in rows to this (only switches off). Tests:
  `BootstrapProfilesTest`.
- **Test:** 1. `/profiles`: 5 profiles. 2. Open Always-on and check which collectors are on.

### F31. Timetable
- **What:** sessions start and stop on a weekly schedule.
- **Trigger:** `POST /timetable`, `POST /timetable/{entry}/toggle`, `DELETE /timetable/{entry}`
  (instructor and up); cron F107.
- **Code:** `ProfileController` (`saveEntry`, `toggleEntry`, `deleteEntry`), `SessionManager::timetableTick`.
- **In/out/side effects:** writes `timetable_entries`; starts/stops `lab_sessions`.
- **Status:** working (`timetable_starts_and_stops_sessions`).
- **Test:** 1. Add an entry starting in 2 minutes. 2. Keep the scheduler running. 3. A session starts on time.

### F32. Policy delivery to agents
- **What:** each agent fetches its effective policy (profile + session + overrides + signatures
  version + PAC URL + screen config).
- **Trigger:** `GET /api/agent/v1/policy` (agent); also pushed by `policy_updated`.
- **Code:** `AgentController::policy`, `S/Services/PolicyService.php` (`forPc`, `effective`, `response`).
- **Status:** working (`policy_includes_browser_ai_overview_suppression`,
  `policy_builder_screen_config_handles_direct_port_and_tls_reverse_proxy`).
- **Test:** 1. Change the session profile. 2. The agent logs "policy applied" within seconds.

### F33. Student notice in policy
- **What:** the notice text (from settings/profile) is sent to PCs and shown to students.
- **Code:** `PolicyService` (`notice_text`), helper (F94).
- **Status:** partly working. ⚠PRIV the notice was cut down and now says screens are never
  recorded, which HEAD contradicts (`docs/HISTORY.md` batch 5).
- **Test:** 1. Change `notice_text` in Settings. 2. Log in on a lab PC: the new notice shows.

---

## 6. Commands to PCs

All commands go through `S/Services/CommandBus.php::send`, are stored in `commands`, broadcast
on `private-pc.<id>` (`S/Events/AgentCommand.php`) and acknowledged in the next event batch
(`command_result`). Agent side: `A/core/src/agent.cpp:1406` (`handle_command`).

### F34. Send a message to students
- **Trigger:** `POST /sessions/message` (`text`, optional `pc_ids`, `urgent`). Assistant and up.
- **Code:** `SessionController::message`; agent `message` → helper `notify`.
- **Side effects:** command row; audit; tray notification plus a modal dialog on Windows.
- **Status:** working (`commands_are_executed_and_acknowledged`, agent test).
- **Test:** 1. Send "hello" to one PC. 2. The student sees it.

### F35. Kill a process
- **Trigger:** `POST /pcs/{pc}/kill` (`exe` name or `pid`). Assistant and up.
- **Code:** `SessionController::kill`; agent `kill` (exempt list: system processes).
- **Status:** working (`agent kills when the policy opts in, but never exempt processes`).
- **Test:** 1. Open a text editor on a PC. 2. Kill it from the Processes tab.

### F36. Isolate a PC from the Internet
- **What:** cuts Internet access on one PC (LAN and CCC stay reachable) and closes AI apps.
- **Trigger:** `POST /pcs/{pc}/isolate`. Assistant and up.
- **Code:** `SessionController::isolate`, `SessionManager::isolate`; agent `isolate_notice` +
  policy `isolate`; Linux `A/platform/linux/network_isolator.cpp` (nft via `popen`), Windows
  `A/platform/windows/isolator_win.cpp`.
- **Status:** working on Linux (`isolate cuts the Internet (LAN + CCC kept)…`); Windows unclear.
- **Test:** 1. Isolate a PC. 2. On it, `curl https://example.com` fails and the CCC page still loads.

### F37. Rescan
- **What:** asks a PC to rescan processes, extensions/installed, artifacts, network/ports/browser,
  or devices now.
- **Trigger:** `POST /pcs/{pc}/rescan` (`what`, `kind`). Assistant and up.
- **Code:** `SessionController::rescan`; agent `rescan`.
- **Status:** working (`rescan_endpoint_dispatches_specific_target_for_what_and_kind`).
- **Test:** 1. Click Rescan on the Installed tab. 2. The tab refreshes within ~15 s.

### F38. Ping and flush
- **What:** makes the agent send its queued events now.
- **Trigger:** internal command types `ping`, `flush`.
- **Status:** working (agent command test).
- **Test:** covered by tests; not exposed as a button.

---

## 7. Alerts and review

### F39. Alert creation from events
- **What:** rule hits, tamper events, paste over a size threshold, network events and machine
  silence become alerts, deduplicated.
- **Trigger:** agent `POST /api/agent/v1/events` (batches); proxy `POST /api/internal/net-events`.
- **Code:** `S/Services/EventIngestor.php::ingest`, `refreshLiveScore`; `S/Models/Alert.php`.
- **In/out/side effects:** writes `events`, `alerts`, `key_stats`, `paste_events`, `net_events`;
  updates `pcs` live score; broadcasts `AlertRaised`/`PcUpdated`; deletes older scan snapshots.
- **Status:** working (`batch_ingest_alerts_dedup_and_ack`,
  `test_mode_extension_and_local_model_rule_hits_create_alerts`).
- **Test:** 1. Start a Test session. 2. Open an AI app on a PC. 3. An alert appears on `/alerts`.

### F40. Alerts list and filters
- **Trigger:** `GET /alerts` (+ `.html`). Assistant and up.
- **Code:** `S/Http/Controllers/Web/AlertController.php::index`.
- **Status:** working (`alerts_open_filter_and_json_acknowledge_handle_retired_pcs`).
- **Test:** 1. Open `/alerts`. 2. Filter "open": only unacknowledged alerts.

### F41. Acknowledge alerts (single and batch)
- **What:** staff mark alerts reviewed, with a comment; the seat turns green again.
- **Trigger:** `POST /alerts/{alert}/ack`, `POST /alerts/ack-batch` (up to 500 IDs).
- **Code:** `AlertController::acknowledge`, `acknowledgeBatch`.
- **Status:** working (`acknowledging_all_alerts_turns_seat_green_and_resets_live_score`).
  ⚠SEC batch has no lab scoping and re-stamps already acknowledged alerts.
- **Test:** 1. Acknowledge all alerts for one PC. 2. Its seat turns green.

### F42. Live score per PC
- **What:** a running risk indicator on each seat from open alerts.
- **Code:** `EventIngestor::refreshLiveScore`, `reconcileLiveScores`.
- **Status:** working (`dashboard_reconciles_stale_live_score_when_no_open_alerts_exist`).
- **Test:** see F41.

---

## 8. Screen viewing and evidence

### F43. Live screen view of a PC ⚠PRIV ⚠SEC
- **What:** instructors and admins (not assistants) watch a PC's screen live (single view, screen wall, seat-to-seat switching).
- **Trigger:** dashboard → `POST /api/screen/pc/{pc}/token` or `POST /api/screen/{student}/token`
  → browser opens `ws(s)://<ccc>:8082/view` (or `/screen-gw/view` via nginx) and sends the token as its first message.
- **Code:** server `S/Http/Controllers/Api/ViewTokenController.php`; gateway
  `xephyguard-screen/gateway/viewer_handler.go`, `auth.go`; viewer `xephyguard-screen/viewer/`;
  agent side `A/apps/screen_streamer.cpp` (inside the helper), `xephyguard-screen/capture/`.
- **In/out/side effects:** writes `screen_view_audits`; 30 s JWT leases; gateway keeps the last
  keyframe in RAM.
- **Status:** partly working (`owning_instructor_gets_valid_jwt_and_audit`, `lease_renewal_and_stream_stop`,
  `issue_token_directly_for_pc_without_session`). Problems: plaintext `ws://` by default, privacy mask is wired in the agent but the server sends no mask rules yet, conflicts
  with ETHICS.md (`docs/HISTORY.md`).
- **Test:** 1. Start the gateway and a helper on a PC. 2. Click the seat's Screen tab. 3. The desktop shows.

### F44. Screen view lease renew and stop
- **Trigger:** `POST /api/screen/{audit}/renew`, `POST /api/screen/{audit}/stop`.
- **Code:** `ViewTokenController::renewToken`, `stopView`; writes `screen_view_audits`.
- **Status:** working (`lease_renewal_and_stream_stop`).
- **Test:** 1. Keep a view open more than 30 s: it keeps playing. 2. Close it: the audit row gets an end time.

### F45. Screen gateway (relay)
- **What:** Go WebSocket relay between agent streams and browser viewers.
- **Trigger:** `xephyguard-screen/gateway` binary (`-addr`, `-secret`, `-issuer`, `-audience`,
  `-trusted-proxy`, `-server-url`, `-allowed-origins`, `-dev-insecure`; env `SCREEN_GATEWAY_INTERNAL_TOKEN` required outside
  dev); endpoints `/agent`, `/view`, `/health` (also under `/screen-gw/`), loopback `POST /internal/revoke`.
- **Code:** `xephyguard-screen/gateway/main.go`, `stream.go`, `agent_handler.go`, `viewer_handler.go`, `auth.go`.
- **Status:** partly working (Go vet and `go test -race` pass locally, not in CI yet; no binary in git,
  build it from source). Browser Origin checked (request host or `-allowed-origins`); agents authenticate only by the nginx-verified mTLS cert
  and stream only as the PC that cert belongs to (looked up on the server).
- **Test:** 1. `cd xephyguard-screen/gateway && SCREEN_GATEWAY_INTERNAL_TOKEN=x go run . -secret x`. 2. `curl localhost:8082/health`.

### F46. Screenshot evidence ⚠PRIV
- **What:** staff save a still image of a PC's screen as evidence with a note.
- **Trigger:** `POST /pcs/{pc}/evidence` (png/webp/jpeg ≤ 8 MB, monitor, note);
  `GET /evidence/{capture}`. Instructor and up, only with `screen_view_id` of the caller's own open
  screen view of that PC; every `show` is audited (`evidence.view`) (S-12).
- **Code:** `S/Http/Controllers/Web/EvidenceController.php` (`store`, `show`).
- **In/out/side effects:** encrypted file at `storage/app/evidence/pc_<id>/<uuid>.bin` (S-13; `APP_KEY`); `evidence_captures`
  row; audit. Purged with raw data (F109).
- **Status:** working (`instructor_can_upload_view_and_report_screen_evidence`,
  `invalid_file_is_rejected_and_retention_purges_expired_evidence`). Conflicts with ETHICS.md;
  any staff can upload any image (not tied to the stream).
- **Test:** 1. Open a PC's screen. 2. Click "Save evidence". 3. It shows in the session report.
- **Planned:** kept (owner, 2026-11-17). Instructor and up only, accepted only during an audited screen view,
  every `show` audited (S-12); stored as sha256 + `Crypt`-encrypted blob (S-13) with a short retention (S-37).

### F47. Standalone screen agent (xgagent)
- **What:** retired (DECISIONS #25). It was a separate screen-only agent from the WP0–WP6 work.
- **Code:** program and CMake target removed (docs/plan/screen.md Step 43). `xephyguard-screen/capture/`
  and `core/` stay as the shared capture/encode library used by the helper-embedded streamer (F95, F43)
  and `xgbench`.
- **Status:** retired. Screen streaming is only through the helper-embedded streamer (F43).
- **Test:** none; nothing to test.

---

## 9. Scoring, reports and research

### F48. Session scoring (v1 rules + v2 Isolation Forest)
- **What:** after a session, each student gets an explainable score with ranked reasons (v1) and an
  anomaly score (v2) as flags for human review.
- **Trigger:** session stop → job `ScoreSession`; `POST /reports/{session}/rescore`; CLI
  `python -m xg_analysis score`.
- **Code:** `S/Jobs/ScoreSession.php`, `S/Services/AnalysisClient.php`, `analysis/xg_analysis/scoring.py`, `features.py`.
- **In/out/side effects:** HTTP call to the analysis service; writes `scores`, `reports`.
- **Status:** working (`v1_scores_are_explained_and_ranked`, `v2_isolation_forest_alongside_v1`,
  `personal_baseline_z_scores`, `exempt_student_behavior_signals_ignored`).
- **Test:** 1. Run a short Test session with some AI use. 2. Stop it. 3. `/reports` shows scores with reasons.

### F49. Session reports list and detail (HTML)
- **Trigger:** `GET /reports` (+ `.html`), `GET /reports/{session}`, `GET /report-session-{num}.html`.
  Instructor and up.
- **Code:** `S/Http/Controllers/Web/ReportController.php` (`index`, `show`, `showByNumber`).
- **Status:** working (`report_show_and_pdf_render_when_blocked_net_events_exist`, `ReportRenderTest`).
  Score and reasons come from `ReportModel::scoreRows`, as stored (no inversion, no default points),
  with signal IDs and a not-observable column. Review comments are still not shown.
- **Test:** 1. Open a report. 2. A student's score, reasons and not-observable text match the PDF and CSV (F51).

### F50. Student report
- **Trigger:** `GET /reports/{session}/students/{student}`.
- **Code:** `ReportController::student`.
- **Status:** unclear (no dedicated test).
- **Test:** 1. Click a student in a report. 2. The timeline and reasons show.

### F51. PDF and CSV export
- **Trigger:** `GET /reports/{session}/pdf`, `GET /reports/{session}/csv`.
- **Code:** `ReportController::pdf` (dompdf, `resources/views/reports/pdf.blade.php`), `csv`; audit.
- **Status:** working (PDF rendered in the report test; CSV in `ReportRenderTest` and F051 test). PDF and CSV
  list every reason with points and signal, plus a not-observable column (CSV column `top_reasons` now holds all reasons).
- **Test:** 1. Download both. 2. The PDF opens; the CSV has one row per student.

### F52. Research export
- **What:** pseudonymised export of consenting students only.
- **Trigger:** `GET /research/export` (instructor and up). The analysis CLI `python -m xg_analysis export` was removed (analysis Step 13, DECISIONS #30); for Parquet, convert the CSV (`pandas.read_csv(f).to_parquet(...)`).
- **Code:** `ReportController::research` → `App\Services\ResearchExport` (consent filtered in SQL, streamed with `lazyById`); audit.
- **Status:** working. Always the pseudonym, never the student number; the F57 switch does not apply (S-41, `ResearchExportTest`). CSV only.
- **Test:** 1. Give one student consent (F55). 2. Export: only that student, pseudonymised.

### F53. Rescore
- **Trigger:** `POST /reports/{session}/rescore`.
- **Code:** `ReportController::rescore` → `ScoreSession` job; audit.
- **Status:** unclear (no test).
- **Test:** 1. Click Rescore. 2. The report's "scored at" time updates.

---

## 10. Roster and consent

### F54. Roster import
- **What:** import students from CSV (`student_id`, `name`, `login`, with or without header).
- **Trigger:** `GET /roster` (+ `.html`), `POST /roster/import` (≤ 2 MB). Instructor and up.
- **Code:** `S/Http/Controllers/Web/RosterController.php::import` → `App\Services\RosterImport::parse`; writes `students`; audit (row and skipped counts).
- **Status:** working (`RosterImportTest`, F054 test). Header columns found by name (any order, extra columns allowed); a header missing `student_id`/`id`, `name` or `login` is rejected; short rows, missing id or login, and values over 255 chars are skipped and listed by line in the status message (S-42).
- **Test:** 1. Upload a 3-row CSV. 2. `/roster` lists 3 students.

### F55. Research consent per student
- **Trigger:** `POST /roster/{student}/consent`.
- **Code:** `RosterController::consent`; writes `consents`; audit.
- **Status:** working through F52's test; the button itself is not tested.
- **Test:** 1. Toggle consent. 2. Check the research export (F52).

### F56. Exempt a student
- **What:** marks a student as exempt (e.g. accommodations) so behaviour signals are ignored.
- **Trigger:** `POST /roster/{student}/exempt`.
- **Code:** `RosterController::exempt`; scoring honours it.
- **Status:** working (`exempt_student_behavior_signals_ignored`).
- **Test:** 1. Exempt a student. 2. Rescore: their behaviour reasons disappear.

### F57. Pseudonymise roster
- **Trigger:** `POST /roster/pseudonymize`. Admin only.
- **Code:** `RosterController::pseudonymize`; writes a setting; audit.
- **Status:** since S-41 the setting only shows or hides the roster's pseudonym column ("Show pseudonyms"); it no longer affects the research export (always pseudonymised, F52). Session reports do not read it. Admin-only check tested in F052.
- **Test:** 1. Turn it off. 2. The roster shows "Not used" in the pseudonym column; the research export still shows pseudonyms.

---

## 11. Signatures and blocklists

### F58. Signature and blocklist pages
- **Trigger:** `GET /signatures` (+ `.html`). Admin only.
- **Code:** `S/Http/Controllers/Web/SignatureController.php::index`.
- **Status:** working (renders for admin).
- **Test:** 1. Open `/signatures`. 2. Blocklist groups, rules and versions show.

### F59. Edit blocklist entries
- **What:** add, import, toggle or delete domains (and host/path rules) in groups (AI chat, IDE
  backends, marketplaces, DoH/DoT, VPN/proxy).
- **Trigger:** `POST /blocklist`, `POST /blocklist/import`, `POST /blocklist/{entry}/toggle`,
  `DELETE /blocklist/{entry}`.
- **Code:** `SignatureController`, `S/Services/BlocklistService.php`; writes `blocklist_entries`;
  rewrites net state; audit.
- **Status:** working (`blocklist_edit_hot_reloads_network_state`).
- **Test:** 1. Add `example.org` to AI chat. 2. In a Test session the PC can't resolve it.

### F60. Edit detection rules
- **What:** save or toggle process/extension/port/artifact/adapter/device rules.
- **Trigger:** `POST /rules`, `POST /rules/{rule}/toggle`.
- **Code:** `SignatureController::saveRule`, `toggleRule`; writes `rules`; audit. `saveRule` sets
  `edited`, so a later bootstrap seed import keeps the edit (S-49).
- **Status:** saving tested (`SignatureSeedImportTest`); toggle has no test.
- **Test:** 1. Disable a rule. 2. After publish (F61/F110) the agent stops flagging it.

### F61. Publish and roll back signature bundles
- **What:** compile and sign the bundle; activate an older version to roll back.
- **Trigger:** `POST /signatures/publish`, `POST /signatures/{version}/activate`; auto-publish F110.
- **Code:** `SignatureController::publish`, `activate`; `SignatureCompiler::publish`, `assertPublishable`.
- **Status:** working (`signature_publish_and_rollback`,
  `publish_refuses_bundle_without_domains_and_keeps_active_version`). Publish signs first, then switches the
  active version in one locked transaction; a signing error keeps the current version. Rollback goes through
  `SignatureCompiler::activate` and refuses a version whose signature does not verify (S-48).
- **Test:** 1. Publish. 2. Activate the previous version. 3. Agents report the older version in heartbeat.

### F62. Agent bundle verification
- **What:** the agent checks the bundle signature against the lab CA and refuses one without
  domains, keeping the last good bundle.
- **Code:** `A/core/src/` (signature verification, `crypto`), `rules`.
- **Status:** working (`signature bundle verification with a CA certificate`,
  `agent refuses a bundle without domains and keeps the last-known-good one`).
- **Test:** covered by tests.

### F63. Compile signatures offline
- **What:** validates `data/signatures/*.yaml` and writes a `signatures.json` (used for the Windows
  installer seed).
- **Trigger:** `python3 scripts/compile_signatures.py [--out file]`.
- **Status:** working (run today: 72 rules).
- **Test:** 1. Run it with `--out /tmp/s.json`. 2. It prints the rule count.

---

## 12. Network enforcement (gateway, DNS, proxy)

### F64. Gateway or proxy-only network mode
- **What:** the CCC is either the lab's router (gateway: DNS + nftables + transparent proxy) or only a
  proxy reached by PAC (proxy_only).
- **Trigger:** setting `network_mode` (F104), `XG_NETWORK_MODE`; installer question.
- **Code:** `NetworkPolicyWriter`, `proxy/xg_netpolicy/generate.py`, `ccc-deploy/docker/proxy-entrypoint.sh`.
- **Status:** partly working (tests pass; gateway mode never run on a real two-NIC host).
- **Test:** 1. Set proxy_only. 2. `curl localhost:8000/proxy.pac` returns a PAC pointing at the CCC.

### F65. DNS sinkhole with per-PC views (Unbound)
- **What:** PCs in a blocking session get NXDOMAIN/sinkhole for blocklisted domains; others resolve
  normally.
- **Trigger:** Docker service `netpolicy`; reads net state.
- **Code:** `proxy/xg_netpolicy/` (`generate.py`, `domains.py`, `state.py`), `proxy/dns/unbound.conf`,
  `ccc-deploy/docker/netpolicy-run.sh`.
- **Status:** working (`generate_outputs`, `state_groups`, `domain_suffix_matching`).
- **Test:** 1. Start a Test session. 2. On the PC `nslookup chatgpt.com <ccc-ip>` is blocked.

### F66. nftables gateway rules
- **What:** forwards lab traffic, redirects HTTP(S) to the proxy, blocks DoH/DoT IPs and VPN ports.
- **Code:** `proxy/nftables/xephyguard.nft`, `xg_netpolicy` (`apply_skips_nft_in_proxy_only_or_without_table`).
- **Status:** partly working (ruleset syntax checked in CI; never run on a real gateway).
- **Test:** 1. On the CCC: `sudo nft list ruleset | grep xephyguard`.

### F67. SNI blocking without decryption
- **What:** the proxy closes TLS connections to blocked hosts from the ClientHello alone.
- **Code:** `proxy/addons/xephyguard_addon.py::tls_clienthello`.
- **Status:** working (`tls_clienthello_blocks_without_decryption`).
- **Test:** 1. In a Test session, `curl https://chatgpt.com` via the proxy fails at TLS.

### F68. Path rules and selective inspection
- **What:** for chosen hosts (`inspect_hosts`) the proxy decrypts and blocks specific paths.
- **Code:** addon `request`, `proxy_logic.py`.
- **Status:** working (`path_rule_blocked`, `plain_http_blocked_domain`).
- **Test:** 1. Add a host/path rule. 2. That path is blocked, the rest of the site works.

### F69. Google AI Overview suppression
- **What:** forces Google Search to web results (`udm=14`), strips AI elements with CSS, sets
  browser policies on PCs.
- **Code:** addon `request`/`response` (CSS injection, CSP nonce relaxation),
  `proxy_logic.py`; agent browser policy writer (F91); setting `google_hide_selectors`.
- **Status:** working (`google_search_rewrite_sets_udm14_…`, `response_css_injection`,
  `csp_nonce_and_style_relaxation`). `eval/live_checks/google_ai_overview.py` checks it against the live site.
- **Test:** 1. In a session, search Google on a PC. 2. The URL gets `udm=14` and no AI Overview shows.

### F70. Observe allowed AI/IDE traffic ⚠PRIV
- **What:** when AI is allowed, the proxy still records host + path of AI/IDE requests and raises
  alerts.
- **Code:** addon `tls_clienthello`, `request`; `S/Http/Controllers/Internal/NetEventsController.php`.
- **Side effects:** `POST /api/internal/net-events` (internal token) → `net_events`, `alerts`.
- **Status:** working (`tls_clienthello_observes_allowed_ai_and_ide_traffic`,
  `observed_ai_and_ide_net_events_raise_alert_and_surface_in_dashboard`). Logs even when `log_urls`
  is off, and raises high-severity alerts while AI is allowed.
- **Test:** 1. Practice: AI allowed. 2. Open chatgpt.com on a PC. 3. A net event shows in the inspector.

### F71. Proxy auto-config (PAC)
- **What:** PAC file pointing PCs at the CCC proxy, used in proxy_only mode.
- **Trigger:** `GET /proxy.pac` (public); pushed to PCs as `proxy_pac_url`.
- **Code:** `PublicController::proxyPac`; agent/helper writes it to the OS (F91).
- **Status:** working (`proxy_pac_reflects_network_mode_and_policy_carries_pac_url`,
  `agent pushes proxy_pac_url to helper in proxy_only mode`). ⚠SEC plain HTTP, falls back to DIRECT.
- **Test:** 1. `curl localhost:8000/proxy.pac`.

---

## 13. Agent: monitoring on lab PCs

The agent (`xephyguard-agent run`, root/SYSTEM service) runs collectors on the policy's intervals
and queues events in a local SQLite queue (`A/core/src/event_queue*`), sent in batches to
`POST /api/agent/v1/events`. Which collectors run depends on the profile's `collect` flags.

### F72. Heartbeat and capabilities
- **What:** every `heartbeat_s` (5 s) the agent reports it is alive, its signature version and which
  collectors actually work on this PC.
- **Events:** `heartbeat`, `capabilities`, `agent_started`, `agent_stopping`.
- **Status:** working (`agent heartbeat reports honest capabilities.gpu…`, `machine_silent_detection`).
- **Test:** 1. `sudo xephyguard-agent status`. 2. The seat shows "last seen" a few seconds ago.

### F73. Process monitoring and AI process detection
- **What:** tracks process start/stop and snapshots; matches against process rules (AI apps, local
  LLM runners, CLI agents).
- **Events:** `process_start`, `process_stop`, `process_snapshot`, `rule_hit`.
- **Code:** `A/core/src/process_tracker.cpp`, `rules`; `A/platform/linux/process_monitor.cpp`,
  `A/platform/windows/process_monitor_win.cpp`.
- **Status:** working on Linux (`agent reports and warns on an AI process in Test mode`,
  `process tracker diffs snapshots`); Windows unclear.
- **Test:** 1. In a Test session run `ollama` (or a dummy named like a rule). 2. A rule hit alert appears.

### F74. Listening ports and local LLM API probe
- **What:** finds listening ports; common dev ports only count if a probe gets an LLM-API-like reply.
- **Events:** `listening_port`, `rule_hit`.
- **Code:** `A/platform/linux/network_monitor.cpp`, `A/platform/windows/network_win.cpp`, agent `scan_ports`.
- **Status:** working (`common dev ports need a positive LLM-API probe`).
- **Test:** 1. Run `python -m http.server 11434`. 2. No alert. 3. A fake `/v1/models` server on 11434 raises one.

### F75. Window focus and titles ⚠PRIV
- **What:** reports the focused app and, depending on `title_capture` (off/app_only/full), the
  window title.
- **Events:** `window_focus`.
- **Code:** helper `A/platform/linux/x11_helper.cpp`, `A/platform/windows/helper_win.cpp`; filter in agent core.
- **Status:** working on X11 (`X11 window monitor survives 1000 rapid window open/close cycles`).
  Raw titles skip `filter_title` in `web.ai_tab` hits.
- **Test:** 1. Set title capture to full. 2. Focus a browser tab. 3. The inspector shows the title.

### F76. Browser AI tab detection
- **What:** flags AI sites seen in browser window/tab titles.
- **Code:** agent core (`web.ai_tab` rule hits), helper title feed.
- **Status:** partly working (see F75; hard-coded browser lists in `agent.cpp`).
- **Test:** 1. Open chatgpt.com in a Test session (if DNS is not blocking). 2. A tab alert appears.

### F77. Paste and copy features
- **What:** for each paste: size, source app, target app, timing; copy events with app names. No
  clipboard text leaves the function (`paste.cpp`).
- **Events:** `paste`, `copy`; alert when over `paste_alert_chars`.
- **Code:** `A/core/src/paste.cpp`, helper `paste_probe`; `EventIngestor`.
- **Status:** working (`clipboard text is reduced to features`,
  `copy_from_chrome_and_paste_in_vscode_updates_dashboard`). ⚠PRIV file names leak into
  copy/paste events (`docs/HISTORY.md` batch 2).
- **Test:** 1. Copy 2,000 chars from a browser into an editor. 2. A paste event with size and apps shows.

### F78. Keystroke timing statistics ⚠PRIV
- **What:** per window of `keystats_window_s`, counts and timing of key presses, never which keys.
- **Events:** `key_stats`.
- **Code:** `A/core/src/keystats.cpp`, `A/platform/linux/input_evdev.cpp`, Windows helper.
- **Status:** working (`key stats aggregate timing without key identity`).
- **Test:** 1. Type for a minute in a session with `keys` on. 2. `key_stats` rows exist for that PC.

### F79. File activity in work directories ⚠PRIV
- **What:** notes file creates/changes in configured `work_dirs` (names/sizes, not content).
- **Events:** `file_activity`.
- **Code:** `A/platform/linux/file_watcher.cpp`, `A/platform/windows/file_watcher_win.cpp`.
- **Status:** unclear (no test; turned on at boot by `4ac2d8e`).
- **Test:** 1. Create a file in a work dir. 2. A `file_activity` event shows in the timeline.

### F80. AI extension inventory and activity
- **What:** lists installed IDE/browser extensions, flags AI ones, and alerts when they are
  actively used (or in Test mode).
- **Events:** `extension_scan`, `rule_hit`.
- **Code:** `A/platform/linux/extension_scanner.cpp`, `A/platform/windows/extensions_win.cpp`, agent core.
- **Status:** working (`agent inventories installed AI extensions and alerts on test mode or active use`,
  `…matches copilot-proxy`). Activity heuristic is weak; IDE lists hard-coded.
- **Test:** 1. Install GitHub Copilot in VS Code. 2. It shows on the Installed tab.

### F81. Local AI model files
- **What:** scans home dirs and removable media for `*.gguf`, `*.safetensors`, Ollama/LM Studio stores.
- **Events:** `artifact_scan` (shown on the Installed tab, not as alerts outside Test mode).
- **Code:** `A/core/src/artifact_scan.cpp`.
- **Status:** working (`artifact scan gives each root its own file budget…`).
- **Test:** 1. `truncate -s 10M ~/model.gguf`. 2. Rescan artifacts. 3. It shows on the Installed tab.

### F82. Network adapters, VPN and tethering
- **What:** detects new adapters, VPNs, phone tethering and a second default route.
- **Events:** `net_adapter`, `rule_hit`.
- **Code:** network monitors (Linux/Windows), `data/signatures/adapters.yaml`.
- **Status:** working (`agent detects tethering, VPN and a second default route`).
- **Test:** 1. Plug in a USB tether. 2. An adapter alert appears.

### F83. USB devices
- **Events:** `device`.
- **Code:** `A/platform/linux/device_monitor.cpp`, `A/platform/windows/device_win.cpp`, `devices.yaml`.
- **Status:** working on Linux (rule engine test); Windows unclear.
- **Test:** 1. Plug in a USB stick. 2. It shows on the USB tab.

### F84. GPU usage sampling
- **What:** samples GPU busy % (a local LLM sign).
- **Events:** `gpu_sample`.
- **Code:** `A/platform/linux/gpu_monitor.cpp`.
- **Status:** working on Intel/AMD Linux (`Linux GpuMonitor reads Intel i915/Xe RC6 residency…`).
- **Test:** 1. Turn on `gpu` in a profile. 2. Run a GPU load. 3. `gpu_sample` events rise.

### F85. Tamper detection
- **What:** reports helper termination, binary hash mismatch (manifest), unclean restarts.
- **Events:** `tamper`, `helper_status`.
- **Code:** agent core (`integrity_check_s`, manifest), service host.
- **Status:** partly working (`unclean_restart_and_command_results`). False "terminated by user" on
  every helper disconnect, including logoff.
- **Test:** 1. Kill the helper process. 2. A tamper alert appears. 3. Log off: one appears too (false positive).

### F86. Raw packet capture on Windows ⚠PRIV
- **What:** `SIO_RCVALL` raw socket as SYSTEM for traffic observation.
- **Code:** `A/platform/windows/network_win.cpp:535`.
- **Status:** unclear (Windows, compile-checked only).
- **Test:** only on a real Windows PC.

---

## 14. Agent: enforcement on lab PCs

### F87. Kill AI processes by policy
- **What:** in Test mode or while isolated, high/critical process hits are killed automatically,
  except an exempt list.
- **Code:** agent core (`actions.kill`, `kill_exempt`).
- **Status:** working (`agent kills when the policy opts in, but never exempt processes`).
  Broad exe names (`aide`, `comet`, `copilot`) can close unrelated programs.
- **Test:** 1. Start a Test session. 2. Run a program matching a kill rule. 3. It closes.

### F88. Re-evaluate on mode change
- **What:** apps already open when a Test starts are checked again under the new policy.
- **Code:** `Agent::reevaluate_all`.
- **Status:** working (`tools already running are re-evaluated when a Test session starts`).
- **Test:** 1. Open an AI app. 2. Start a Test session. 3. It is flagged/killed.

### F89. Network isolation on the PC
- See F36. Linux nftables table `xephyguard_isolate`; Windows firewall in `isolator_win.cpp`.

### F90. Exam lockdown on Windows
- **What:** disables Task Manager (`DisableTaskMgr`) in Test/Exam mode.
- **Code:** `A/platform/windows/system_win.cpp`.
- **Status:** unclear (Windows only). Applied to every Test profile and stays set if the agent crashes.
- **Test:** only on Windows: start a Test session, press Ctrl+Shift+Esc.

### F91. Browser policies and DNS settings on the PC
- **What:** writes Chromium/Edge/Firefox policies (CA trust, PAC, AI Overview suppression) and a
  systemd-resolved drop-in pointing DNS at the CCC; Windows changes HKLM browser/DNS policies.
- **Code:** Linux helper/platform (`Linux browser policy writer…`, `Linux systemd-resolved drop-in…`),
  `installers/linux/install-browser-ca.sh`, `installers/browser-policies/*.json`, Windows `system_win.cpp`.
- **Status:** working on Linux (2 agent tests); Windows unclear.
- **Test:** 1. On Linux, check `/etc/opt/chrome/policies/managed/`. 2. `resolvectl status` shows the CCC.

### F92. Agent CLI and service
- **What:** `xephyguard-agent enroll|run [--foreground]|status|version`, `--config`; systemd unit
  (watchdog) or Windows service.
- **Code:** `A/apps/agent_main.cpp`, `A/platform/linux/service_host.cpp`,
  `installers/linux/xephyguard-agent.service`.
- **Status:** working on Linux (built and tests passed today); Windows service unclear.
- **Test:** 1. `xephyguard-agent version`. 2. `systemctl status xephyguard-agent`.

---

## 15. Session helper (student desktop)

`xephyguard-helper` runs in the student's desktop session (user level) and talks to the agent over
IPC (`A/platform/linux/ipc_unix.cpp`, `A/platform/windows/ipc_pipe_win.cpp`; messages `config`,
`notify`, `indicator`, `paste_probe`, `clear_proxy_pac`).

### F93. Notifications and messages
- **What:** shows staff messages and warnings (desktop notification; on Windows also a modal box).
- **Status:** partly working (Linux tested through the command test; Windows unclear, modal boxes stack).
- **Test:** see F34.

### F94. Monitoring status indicator
- **What:** an on-screen indicator that shows the current mode/monitoring, placed clear of the top bar.
- **Code:** `A/platform/linux/x11_helper.cpp` (`X11StudentUi`).
- **Status:** working on X11 (`Linux indicator geometry avoids top bar…`, `X11StudentUi maps indicator…`).
  Wayland not supported.
- **Test:** 1. Log in on an X11 lab PC. 2. The indicator shows the current mode.

### F95. Embedded screen streamer ⚠PRIV
- **What:** captures the desktop and streams to the gateway when a viewer asks.
- **Code:** `A/apps/screen_streamer.cpp`, `xephyguard-screen/capture/x11_capture.cpp`; config
  `[screen]` / `XG_SCREEN_*`.
- **Status:** partly working (see F43). ⚠SEC X11 shared-memory buffer mode `0777`.
- **Test:** see F43.

### F96. Helper status command
- **Trigger:** `xephyguard-helper status`.
- **Code:** `A/apps/helper_main.cpp`.
- **Status:** unclear.
- **Test:** 1. Run it in the student session.

### F97. PAC and proxy settings for the user
- **What:** the helper applies or clears the PAC URL for the user session in proxy_only mode.
- **Status:** working (agent test for the push); OS side on Windows unclear.
- **Test:** 1. proxy_only mode. 2. Browser proxy settings on the PC show the PAC URL.

---

## 16. Public pages

No login needed for any of these.

### F98. Student notice
- **Trigger:** `GET /notice` (+ `.html`).
- **Code:** `PublicController::notice`, `resources/views/public/notice.blade.php`.
- **Status:** partly working (see F33).
- **Test:** 1. Open `/notice` logged out.

### F99. Downloads and install commands ⚠SEC
- **What:** lists installers in `storage/app/public/downloads` with sizes and ready-to-copy install
  commands with a `<ENROLL_KEY>` placeholder and the CA fingerprint plus check instructions (S-15:
  the key and one-time tokens are only on the authenticated `/enroll` page).
- **Trigger:** `GET /downloads` (+ `.html`), `GET /downloads/{file}` (`.deb .rpm .msi .exe .sha256 .bat`).
- **Code:** `PublicController::downloadsIndex`, `download`; `resources/views/public/downloads.blade.php`.
- **Status:** working (`downloads_layout_handles_guest_no_rail_and_kb_sizes`,
  `downloads_serves_deb_and_generates_http_linux_installer_command`, `download_allows_bat_scripts`).
- **Test:** 1. Open `/downloads` logged out. 2. The enrollment key is visible.

### F100. Linux install script
- **What:** a shell script that installs the agent and enrolls it; used as `curl http://… | sudo bash`.
- **Trigger:** `GET /install/linux.sh`.
- **Code:** `PublicController::linuxInstaller`.
- **Status:** working (installer command test). ⚠SEC plain HTTP piped to root.
- **Test:** 1. `curl -s localhost:8000/install/linux.sh | head`.

### F101. CA certificate download
- **Trigger:** `GET /ca.crt`.
- **Code:** `PublicController::caCert`.
- **Status:** working.
- **Test:** see F3.

### F102. Shared clipboard / text share ⚠SEC ⚠PRIV
- **What:** a page instructors and admins can read and write; keeps the last 20 texts; polls every 2.5 s.
- **Trigger:** `GET /share` (+ `/share.html`), `POST /share`, `GET /share/raw`.
- **Code:** `PublicController::shareIndex`, `shareSave`, `shareRaw`; settings `shared_clipboard`,
  `shared_clipboard_at`, `shared_clipboard_history`.
- **Status:** working; instructor/admin login required since S-8 (before that it had no login and
  worked as a message board between exam seats, `docs/HISTORY.md`).
- **Test:** 1. Open `/share` logged out (redirects to login). 2. Sign in as instructor and post text; the
  label shows your name. 3. As assistant, `/share` is 403. Automated: `tests/Feature/ShareAccessTest.php`.
- **Changed (S-8, 2026-11-18):** kept (owner, 2026-11-17), behind login + instructor role; the label is the
  real user name, no anonymous "Instructor".

---

## 17. Admin: health, settings, audit

### F103. Health page
- **Trigger:** `GET /health` (+ `/health.html`, `/admin-health.html`). Admin only.
- **Code:** `AdminController::health` (uses `Doctor`, agent stats).
- **Status:** working (renders in `every_screen_renders_for_admin`); checks themselves unclear (F4).
- **Test:** 1. Open `/health`. 2. Each check has a green/red state.

### F104. Settings
- **What:** lab name, network mode, CCC IP, retention (raw 7–1095 days, reports 30–3650 days),
  notice text, paste alert size, timezone, title capture per mode, `log_urls`, `log_queries`,
  inspect hosts, Google hide selectors, search template URL.
- **Trigger:** `GET /settings` (+ aliases), `POST /settings`. Admin only.
- **Code:** `AdminController::settings`, `saveSettings`; writes `settings`; rewrites net state; audit.
- **Status:** working (`save_settings_accepts_search_template_url_and_gates_log_queries`).
- **Test:** 1. Change the lab name. 2. The header shows the new name.

### F105. Audit log
- **What:** append-only record of staff actions (who, what, when, IP).
- **Trigger:** `GET /audit` (+ aliases). Admin only. Written by `S/Services/Audit.php::log` across the app.
- **Status:** partly working (`audit_log_is_append_only`). `Audit::log` no longer swallows failures: a failed insert throws and the action fails (S-34). Rows are hash-chained (`prev_hash`); `php artisan xg:audit-verify` names any row whose predecessor was changed or removed and prints the head hash (S-36, `AuditChainTest`).
- **Test:** 1. Acknowledge an alert. 2. `/audit` shows the entry. 3. `php artisan xg:audit-verify` prints `audit chain ok`.

### F106. Internal eval results endpoint
- **What:** red-team harness results for the evaluation page.
- **Trigger:** `GET /api/internal/eval/results` (internal token).
- **Code:** `S/Http/Controllers/Internal/EvalController.php`.
- **Status:** unclear (no test).
- **Test:** 1. `curl -H "Authorization: Bearer $XG_INTERNAL_TOKEN" localhost:8000/api/internal/eval/results`.

---

## 18. Background jobs

Run by `php artisan schedule:work` (Docker service `scheduler`) and `queue:work` (service `worker`).
Defined in `server/routes/console.php`.

### F107. Timetable tick — every minute
- `SessionManager::timetableTick`. Status: working (F31).

### F108. Machine-silent detection — every 5 seconds
- **What:** marks PCs silent after 3 missed heartbeats (`XG_HEARTBEAT_TIMEOUT`) and raises an alert,
  using F25 for the label.
- **Code:** `SessionManager::detectSilent`.
- **Status:** working (`machine_silent_detection`).
- **Test:** 1. Stop an agent. 2. Within ~15 s the seat shows silent and an alert appears.

### F109. Retention purge — daily 03:15
- **What:** deletes raw events, key stats, paste events, net events, commands (incl. message text) and
  evidence images older than `retention_days_raw`, and alerts, scores, screen-view audits,
  student–PC assignments and reports older than `retention_days_reports`. The tables and their class
  are listed in one place; a test fails if a new table is in neither list (S-37).
- **Code:** `S/Jobs/PurgeRetention.php`, `S/Services/RetentionRegistry.php`.
- **Status:** working (`…retention_purges_expired_evidence`).
- **Test:** 1. `php artisan tinker --execute="dispatch_sync(new App\Jobs\PurgeRetention)"` on a copy of the DB.

### F110. Signature auto-publish — every minute ⚠SEC
- **What:** compiles and publishes a new bundle when rules/blocklists changed (no-op otherwise).
- **Code:** `server/routes/console.php:11-20`, `SignatureCompiler::publish(null, 'auto')`.
- **Status:** working (`publish_is_noop_when_nothing_changed`). No review step.
- **Test:** 1. Toggle a rule. 2. Within a minute `/signatures` shows a new active version.

### F111. Session scoring job (queue)
- `S/Jobs/ScoreSession.php`, see F48. Status: working.

---

## 19. Packaging and deployment tools

### F112. Docker Compose stack and installer
- **What:** one-command CCC install on Ubuntu: asks for interfaces, mode, IPs; writes `.env`, state
  dirs, CA; starts `mysql`, `web` (nginx + php-fpm, ports 80/443/8443), `reverb`, `worker`,
  `scheduler`, `netpolicy`, `proxy`, `analysis`, `discovery`.
- **Trigger:** `sudo ccc-deploy/install.sh`; `docker compose -f ccc-deploy/docker-compose.yml up -d`.
- **Code:** `ccc-deploy/install.sh`, `docker-compose.yml`, `docker/*.Dockerfile`, `nginx/xephyguard.conf`.
- **Status:** partly working (compose config valid; `install.sh` never run on a real two-NIC host).
- **Test:** 1. `docker compose -f ccc-deploy/docker-compose.yml config -q`. 2. On a spare VM, run `install.sh`.

### F113. `xephyguard-ccc` admin CLI
- **What:** `doctor`, `status`, `logs [svc]`, `enroll-key`, `backup DIR` (DB dump + CA + settings),
  `update` (build, migrate, restart), `artisan …`.
- **Code:** `ccc-deploy/bin/xephyguard-ccc`.
- **Status:** unclear (only syntax-checked).
- **Test:** 1. `xephyguard-ccc status`. 2. `xephyguard-ccc backup /tmp/xg-backup` and check the files.

### F114. Local dev bootstrap and native run scripts
- **What:** setup scripts for a new teacher server or lab PC (Docker or native, Linux and Windows),
  and daily start/stop/status scripts.
- **Code:** `new_setup_scripts/` (`setup-server-docker.sh`, `setup-server-native.sh`,
  `install-agent.sh`, `setup-server.ps1/.bat`, `deploy-agents.ps1`), `start_server/` (`start-server`,
  `stop-server`, `status-server` for Linux and Windows), `ccc-deploy/bootstrap-local.sh`,
  `ccc-deploy/docker-compose.local.yml`, `ccc-deploy/import-sqlite-to-local.php`.
- **Status:** unclear (no tests; syntax-checked only for the `.sh` files).
- **Test:** 1. `start_server/linux/status-server.sh`. 2. It reports which services run.

### F115. Linux agent package (.deb)
- **What:** CPack `.deb` with the agent, helper, systemd units, default config, postinst/prerm/postrm.
- **Trigger:** `cmake -S agent -B agent/build-rel -DCMAKE_BUILD_TYPE=Release -DXG_BUILD_TESTS=OFF && cmake --build agent/build-rel -j2 && (cd agent/build-rel && cpack)`.
- **Code:** `agent/CMakeLists.txt`, `agent/packaging/`, `installers/linux/*`.
- **Status:** unclear (CI builds it; not built today).
- **Test:** 1. Build it. 2. `sudo apt install ./xephyguard-agent_*.deb` in a VM.

### F116. Windows installer (NSIS)
- **What:** `XephyGuard-Setup-<ver>.exe`: installs agent + helper, service, CA and code-signing cert
  into Root, ACLs, tamper manifest; `/S /KEY=` silent install.
- **Code:** `installers/windows/xephyguard.nsi`, CI job `agent-windows-msvc`, `scripts/sign_binaries.ps1`.
- **Status:** unclear (never run on real Windows). ⚠SEC gives Users read access to the agent key;
  installs a self-signed cert as a root CA; unsigned build in CI.
- **Test:** only on a Windows VM.

### F117. Fleet deployment (Ansible, GPO)
- **Code:** `installers/ansible/site.yml` + roles, `installers/gpo/README.md`, `new_setup_scripts/windows/deploy-agents.ps1`.
- **Status:** unclear.
- **Test:** 1. `ansible-playbook -i inventory.ini installers/ansible/site.yml --check`.

### F118. Release script
- **Code:** `scripts/release.sh`, `scripts/gen_analysis_schema.sh` (analysis test schema from migrations).
- **Status:** unclear.
- **Test:** 1. Read the script first; it may build and tag.

---

## 20. Evaluation tools

### F119. Red-team harness
- **What:** runs scripted bypass attempts (`eval/scenarios/linux.yaml`, `windows.yaml`) against a PC
  and checks the CCC caught them.
- **Trigger:** `python eval/harness/redteam.py --scenarios … --api … --token …`.
- **Status:** working (`eval/tests`, 4 tests).
- **Test:** 1. `python3 -m pytest -q eval/tests`.

### F120. Load simulator (fake agents)
- **Trigger:** `python eval/simulator/fake_agents.py --ccc … --n 50 --minutes 5 --key … --ca …`.
- **Status:** unclear. Heavy: do not run large `--n` on the 8 GB machine.
- **Test:** 1. `--n 2 --minutes 1` against a local CCC.

### F121. Metrics export
- **Trigger:** `python eval/export/metrics.py --db … --labels … --out …`.
- **Status:** unclear.

### F122. Agent overhead and live Google check
- **Code:** `eval/harness/agent_overhead.sh`, `eval/live_checks/google_ai_overview.py`.
- **Status:** unclear (needs a real PC / internet).

---

## Environment variables

### Server (`server/.env`, Docker `ccc-deploy/.env`)
| Variable | Default | Used for |
|---|---|---|
| `APP_KEY` | — (required) | encryption; tests fail without it |
| `APP_ENV`, `APP_DEBUG`, `APP_URL`, `APP_NAME`, `APP_TIMEZONE`, `APP_LOCALE` | Laravel defaults | app basics |
| `DB_CONNECTION`, `DB_HOST`, `DB_PORT`, `DB_DATABASE`, `DB_USERNAME`, `DB_PASSWORD`, `DB_URL` | sqlite | database (MySQL 8.4 in Docker) |
| `DB_ROOT_PASSWORD` | — | MySQL container root |
| `SESSION_DRIVER`, `SESSION_SECURE_COOKIE`, `CACHE_STORE`, `QUEUE_CONNECTION`, `BROADCAST_CONNECTION`, `LOG_CHANNEL` | Laravel defaults | sessions, cache, queue, broadcasting |
| `REVERB_APP_ID`, `REVERB_APP_KEY`, `REVERB_APP_SECRET`, `REVERB_HOST`, `REVERB_PORT`, `REVERB_SCHEME`, `REVERB_SERVER_HOST`, `REVERB_SERVER_PORT` (+ rate-limit/scaling options) | port 443/8080 | WebSocket server (dev: 8081) |
| `XG_LAB_NAME` | Computer Lab | default lab name |
| `XG_CCC_IP` | 127.0.0.1 | CCC address on the lab LAN |
| `XG_LAB_SUBNET` | 192.168.10.0/24 | lab subnet |
| `XG_NETWORK_MODE` | gateway | `gateway` or `proxy_only` |
| `XG_LAN_IF`, `XG_WAN_IF` | lan0, wan0 | gateway interfaces |
| `XG_PROXY_PORT` | 8080 | proxy port |
| `XG_AGENT_PORT` | 8443 | mTLS agent endpoint |
| `XG_AGENT_CERT_DAYS` | 825 | agent cert lifetime |
| `XG_AGENT_RATE` | 120 | agent API rate limit |
| `XG_HEARTBEAT_TIMEOUT` | 45 | seconds until silent |
| `XG_RETENTION_RAW_DAYS`, `XG_RETENTION_REPORT_DAYS` | 140, 1825 | initial retention |
| `XG_CA_DIR` | storage/app/private/ca | CA files |
| `XG_DATA_DIR` | ../data | signatures and blocklists |
| `XG_NET_STATE` | storage/app/net/state.json | network state file |
| `XG_DISCOVERY_PORT` | 47474 | UDP discovery |
| `XG_ANALYSIS_URL` | http://analysis:8090 | scoring service |
| `XG_INTERNAL_TOKEN` | — | proxy → CCC internal API |
| `XG_INTERNAL_URL` | http://app | where the proxy posts net events |
| `XG_DEPLOY`, `XG_VERSION`, `XG_SCORING_V` | — | deploy mode, image tag, scoring version |
| `SCREEN_GATEWAY_URL`, `SCREEN_GATEWAY_PORT` (8082), `SCREEN_GATEWAY_PATH` (/screen-gw) | | where browsers and agents reach the gateway |
| `SCREEN_GATEWAY_SECRET` | — | JWT signing secret (shared with the gateway) |
| `SCREEN_GATEWAY_AGENT_SECRET` | — | shared token for screen agents |
| `SCREEN_GATEWAY_ISSUER`, `SCREEN_GATEWAY_AUDIENCE`, `SCREEN_GATEWAY_TTL` (30) | | JWT claims and lease |
| `AWS_*`, `POSTMARK_API_KEY`, `RESEND_API_KEY`, `SLACK_*`, `REDIS_*` | — | Laravel boilerplate; **not used** by any XephyGuard feature |

### Screen gateway (Go)
`SCREEN_GATEWAY_ADDR` (:8082), `SCREEN_GATEWAY_SECRET`, `SCREEN_GATEWAY_AGENT_SECRET`,
`SCREEN_GATEWAY_ISSUER`, `SCREEN_GATEWAY_AUDIENCE`, `SCREEN_GATEWAY_DEV_INSECURE` (false; never in production).

### Agent and helper
`XG_CCC_HOST`, `XG_HTTP_PORT`, `XG_SCREEN_ENABLE` (override `agent.toml`); `XG_IPC_ALLOW_ANY=1` (dev only: any local user may talk to the
agent IPC); reads `DISPLAY`, `XDG_SESSION_TYPE`, `DBUS_SESSION_BUS_ADDRESS`, `NOTIFY_SOCKET`, `WATCHDOG_USEC`.

### Python services
`XG_DB_URL`, `XG_ANALYSIS_PORT` (analysis); `XG_DISCOVERY_PORT`, `XG_CCC_IP`, `XG_AGENT_PORT`,
`XG_CA_DIR`, `XG_LAB_NAME` (discovery); `XG_INTERNAL_TOKEN`, `XG_NETWORK_MODE` (proxy).

---

## Config options

### Agent `agent.toml` (default: `installers/linux/agent.toml.default`)
| Section | Keys |
|---|---|
| `[ccc]` | `host`, `api_port` (8443), `http_port`, `discovery_port`, `ca_fingerprint` |
| `[identity]` | `pc_id`, `token`, `enrollment_id`, `poll_token` (written by enroll) |
| `[paths]` | `data_dir`, `log_dir`, `ca_file`, `cert_file`, `key_file`, `helper`, `manifest`, `ipc` |
| `[screen]` | `enabled`, `port` (8082), `path`, `tls` (**false** by default), `token` |
| `[logging]` | `level` |

### Profile / policy fields (sent to agents, edited on `/profiles`)
- `mode`: `always_on`, `practice`, `test`.
- `collect`: `processes`, `ports`, `extensions`, `network`, `devices`, `artifacts` (default on);
  `window`, `clipboard`, `keys`, `files`, `gpu` (default off in code, but turned on by the built-in
  profiles).
- `intervals`: `heartbeat_s`, `process_scan_s`, `snapshot_s`, `port_scan_s`, `extension_scan_s`,
  `artifact_scan_s`, `gpu_sample_s`, `keystats_window_s`, `integrity_check_s`.
- `actions`: `alert`, `warn`, `log`, `kill`, `isolate`; `kill_exempt`; `disabled_rules`.
- `title_capture`: `off`, `app_only`, `full`.
- Proxy/browser: `proxy_mode`, `proxy_pac_url`, `suppress_ai_overview`, `google_search_url`,
  `google_udm14`.
- Other: `work_dirs`, `notice_text`, `session_id`, `signatures_version`, screen config.

### CCC settings (`settings` table, edited on `/settings`)
`lab_name`, `network_mode`, `ccc_ip`, `retention_days_raw`, `retention_days_reports`, `notice_text`,
`paste_alert_chars`, `timezone`, `title_capture_always_on`, `title_capture_practice`,
`title_capture_test`, `log_urls`, `log_queries`, `inspect_hosts`, `google_hide_selectors`, search
template URL; plus internal keys `shared_clipboard`, `shared_clipboard_at`, `shared_clipboard_history`,
pseudonymisation flag.

### Server config files
`server/config/xephyguard.php` (all `XG_*` above), `server/config/services.php` (`screen_gateway`),
`server/config/reverb.php`, `server/phpunit.xml` (test env: SQLite in memory).

### Data files
`data/blocklists/*.txt` (`ai_domains`, `ide_ai_backends`, `marketplaces`, `doh_dot`, `doh_ips`,
`vpn_proxy`), `data/signatures/*.yaml` (`processes`, `extensions`, `ports`, `artifacts`,
`adapters`, `devices`).

---

## Third-party integrations

| Integration | Where | Purpose |
|---|---|---|
| Laravel 13 + Reverb (Pusher protocol), Laravel Echo + pusher-js, Alpine.js, Tailwind, Vite | server | web app, live updates, UI |
| barryvdh/laravel-dompdf | server | PDF reports |
| firebase/php-jwt | server | screen view tokens |
| symfony/yaml | server | reading `data/signatures` |
| MySQL 8.4 (SQLite for dev/tests) | server | database |
| OpenSSL (PHP ext and libssl 3) | server, agent | CA, mTLS, bundle signing |
| nginx | ccc-deploy | TLS termination, mTLS on :8443, Reverb proxy |
| mitmproxy ≥ 11 | proxy | SNI blocking, selective inspection, Google rewrite |
| Unbound | proxy | DNS sinkhole with per-PC views |
| nftables | proxy, agent (Linux) | gateway rules, PC isolation |
| pandas, numpy, scikit-learn, pyarrow, pymysql | analysis | scoring, Isolation Forest (pyarrow unused since analysis Step 13; drop in Step 27) |
| Go: coder/websocket, golang-jwt/jwt v5 | screen gateway | WebSocket relay, JWT check |
| SQLite3, nlohmann/json, toml++, Catch2 | agent | event queue, JSON, config, tests |
| X11 (Xfixes, XShm, XDamage, XRandR), evdev, systemd, D-Bus | agent (Linux) | window, input, capture, service |
| WinHTTP, GDI/DXGI, Windows Registry, Windows Firewall, `SIO_RCVALL` | agent (Windows) | stream client, capture, policies, isolation, packet capture |
| Chromium/Edge/Firefox enterprise policies | installers, agent | CA trust, PAC, AI Overview suppression |
| Google Search (`udm=14`) | proxy, agent | AI Overview suppression (external site behaviour) |
| Docker / Docker Compose, NSIS, CPack, Ansible, GPO, GitHub Actions | deploy, CI | packaging and rollout |

**No outbound integrations**: no email, SMS, Slack or cloud APIs are called. The only outbound HTTP
call from the CCC is to its own analysis service (`AnalysisClient`).

---

## Database tables

From `server/database/migrations/`.

| Table | Holds |
|---|---|
| `labs` | the lab (name, CCC IP, enrollment key, auto-approve flag and MAC list) |
| `seats` | seat map positions |
| `pcs` | enrolled PCs (name, hostname, MACs, machine_uid, OS, IP, status, live score, retired_at) |
| `enrollments` | enrollment requests (CSR, poll token, status) |
| `agents` | agent credentials per PC (token, cert fingerprint, last_seq, revoked_at) |
| `commands` | commands sent to PCs and their results |
| `profiles` | monitoring/blocking profiles |
| `lab_sessions` | sessions (profile, started_by, start/stop) |
| `session_pcs` | PCs in a session (student, override) |
| `timetable_entries` | scheduled sessions |
| `rules` | detection rules |
| `blocklist_entries` | blocked domains/paths by group |
| `signature_versions` | signed bundles and which is active |
| `events` | raw agent events (purged after `retention_days_raw`) |
| `alerts` | alerts with severity, reasons, acknowledgement |
| `key_stats` | keystroke timing windows |
| `paste_events` | paste features |
| `net_events` | proxy/DNS observations |
| `scores` | per-student scores and reasons |
| `reports` | generated reports |
| `screen_view_audits` | who watched which screen, when |
| `evidence_captures` | stored screenshot evidence (file path, note) |
| `students` | roster |
| `student_pc_assignments` | which student sat at which PC |
| `consents` | research consent |
| `users` | staff accounts and roles |
| `audit_log` | staff actions (append-only) |
| `settings` | key/value settings (JSON) |
| `password_reset_tokens`, `sessions`, `cache`, `cache_locks`, `jobs`, `job_batches`, `failed_jobs` | Laravel framework tables |
