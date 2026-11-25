# Plan: server/ (Laravel 13 CCC, Reverb, dashboard, reports)

Sources: [audit/server.md](../audit/server.md) (I-SERVER-1..42), [features/server/](../features/server/README.md) (F1-F7, F10-F13, F15-F34, F37, F39-F42, F44, F46, F49-F61, F71, F98-F111).
Plan written 2026-11-17. Nothing run while planning.
**Run order:** [server-implementation.md](server-implementation.md) merges these steps with the alternatives table (2026-11-18); follow it, not the numbering here.

Rules for every step: one component, <300 changed lines, one kind of change, test added in the same step, then
`cd server && APP_KEY="base64:$(openssl rand -base64 32)" php artisan test` and `./vendor/bin/pint --test <touched files>` (keep touched files clean).
**[APPROVAL]** = changes a documented feature or ETHICS item; wait for the owner. **[CROSS]** = another component's plan has the matching step; update docs/PROTOCOL.md in the same step if a message changes.
Note: commit e9ded61 deliberately restored credentials on `/downloads`; Step 8 reverses part of that, so it needs your decision.

## Phase 1: Critical/High security and bugs
Screen access chain:
- [x] Step 1: `ViewTokenController` ignores client `stream_id`; derive it from the authorised PC/student and put `pc_id` into every JWT, including the renewed one (fixes I-SERVER-1, I-SERVER-30 part; touches F43, F44) [CROSS: gateway must check `pc_id`]
- [x] Step 2: Screen routes get `role:instructor` + active-user check; explicit authorisation rule when the PC has no active session (fixes I-SERVER-3; touches F43, F44)
- [x] Step 3: (revised by DECISIONS #24) `PolicyBuilder::screenGatewayConfig` stops sending `agent_secret`; the agent needs no screen credential, only the `wss://<ccc>:8443/screen-gw/agent` URL (its mTLS cert identifies the PC) (fixes I-SERVER-11; touches F32, F45) [CROSS: after screen Step 62; PROTOCOL.md]

Access control and enrollment:
- [x] Step 4: Move `POST /share` and `GET /share/raw` into the `auth` + role group (fixes I-SERVER-2 part; touches F102) (done as S-8)
- [x] Step 5: Owner decision: remove F102 or keep it authenticated with no anonymous "Instructor" label (fixes I-SERVER-2; touches F102) [APPROVAL]
- [x] Step 6: First-run wizard requires `XG_SETUP_TOKEN` (env) or localhost (fixes I-SERVER-22; touches F1, F2) [CROSS: ccc-deploy prints the token, I-CCC-6] (done as S-9)
- [x] Step 7: Remove `XG_REQUIRE_MTLS=false`; always require `X-SSL-Client-Verify=SUCCESS` + fingerprint match (fixes I-SERVER-6; touches F20, F21) (done as S-10)
- [x] Step 8: Enrollment key off the public `/downloads` page; show on authenticated `/enroll` only, or per-PC one-time token (fixes I-SERVER-4 part 1; touches F99, F17, F6, F15) [APPROVAL: reverses e9ded61] (done as S-15)
- [x] Step 9: Auto-approve by MAC requires human approval or a one-time token (fixes I-SERVER-4 part 2; touches F16, F15) [APPROVAL: changes F16] (done as S-16)
- [x] Step 10: Never adopt an existing PC by MAC/hostname without `machine_uid` or explicit staff choice; flag conflicts (fixes I-SERVER-5; touches F15, F19, F20) (done as S-17)

Privacy defaults (ETHICS):
- [x] Step 11: Built-in profiles match the ETHICS table (Always-on: window/clipboard/keys off, files only in Test, `title_capture` off/app_only) (fixes I-SERVER-7; touches F30, F29, F75, F77, F78, F79) [APPROVAL: changes F30] (done as S-21)
- [x] Step 12: `PolicyBuilder` default `title_capture` = `off`/`app_only`; one source of truth (profile or setting) documented in DECISIONS.md (fixes I-SERVER-8; touches F32, F104, F29) (done as S-20)
- [x] Step 13: (done as S-41) Research export always uses the pseudonym; the toggle no longer affects it (fixes I-SERVER-9, I-ANALYSIS-4; touches F52, F57, F55)
- [x] Step 14: Evidence screenshots: instructor+ only, tied to an audited screen view, audit every `show` (fixes I-SERVER-10 part, I-SERVER-38; touches F46)  (done as S-12)
- [x] Step 15: Owner decision: ethics approval for F46 or remove it (fixes I-SERVER-10; touches F46) [APPROVAL] (done as S-13)

## Phase 2: Tooling (lint, types)
- [x] Step 16: `composer test` and `composer lint` scripts wrapping the commands in CLAUDE.md (no new packages) (fixes none; touches none)
- [x] Step 17: Pint formatting only for `app/Http/Controllers/Web` (no logic; split by directory if >300 lines) (fixes none; touches F10-F111 web pages)
- [x] Step 18: Pint formatting only for `app/Services`, `app/Jobs`, `app/Http/Middleware` (fixes none; touches F20, F32, F108-F111)
- [x] Step 19: Pint formatting only for `routes`, `config`, `database`, `tests` (fixes none; touches none)
- [x] Step 20: Make the CI pint job blocking once Steps 17-19 are in (needs approval to edit CI) (fixes none) [APPROVAL]
- [ ] Step 21: Larastan/PHPStan level 5 would be a new dev dependency: ask first. Until then use `declare(strict_types=1)` on files touched by later steps (fixes none)

## Phase 3: Config and secrets
- [x] Step 22: `NetworkPolicyWriter` writes `state.json` with mode 0640 (umask), or pass the internal token to sidecars via env (fixes I-SERVER-17; touches F5) [CROSS: proxy I-PROXY-13] (done as S-24)
- [ ] Step 23: Reverb `allowed_origins` limited to the dashboard origin from config (fixes I-SERVER-42; touches F13, F21)
- [ ] Step 24: Serve PAC over HTTPS and remove the `DIRECT` fallback in Test mode (fixes I-SERVER-24; touches F71, F32) [CROSS: ccc-deploy nginx]
- [ ] Step 25: Install script and CA fingerprint delivered over HTTPS/pinned fingerprint instructions (fixes I-SERVER-23; touches F100, F101, F99) [CROSS: ccc-deploy I-CCC-2]
- [ ] Step 26: Move inspect hosts, Google hide selectors, port probe paths to `data/` YAML and seed them (fixes I-SERVER-36; touches F5, F59, F69) [CROSS: data/, proxy I-PROXY-21]
- [x] Step 27: Default notice text states live viewing and screenshots truthfully (fixes I-SERVER-12; touches F33, F98) [APPROVAL; same text as agent Step 38] (done as S-25)

## Phase 4: Structure (moves only)
- [x] Step 28: Move inspector query code from `DashboardController` into an `InspectorQueries` service, behavior identical (fixes none; prepares I-SERVER-34, I-SERVER-41; touches F24) (done as S-43)
- [x] Step 29: Move the unacknowledged-alert count out of `layouts/app.blade.php` into a view composer, behavior identical (fixes none; prepares I-SERVER-41; touches F40) (done as S-33)

## Phase 5: Duplication and dead code
- [x] Step 30: (done as S-45) Wire `AgentHealthClassifier` into `detectSilent` and store its reasons in alert details (keeps F25/F108 as documented) (fixes I-SERVER-35; touches F25, F108)
- [x] Step 31: Update docs/PROTOCOL.md for `observed` action and `network/ports/devices` rescan (fixes I-SERVER-37; touches F37, F70) (done as S-26)

## Phase 6: Errors, logging, validation
- [x] Step 32: `Audit::log` reports failures and fails the action (or queues retry) (fixes I-SERVER-14 part; touches F105) (done as S-34)
- [x] Step 33: `EventIngestor` validates type and length of heartbeat strings, `description`, `subject` (fixes I-SERVER-25; touches F39, F72) (done as S-28)
- [x] Step 34: Per-event-type field allow-list from PROTOCOL.md; drop titles not allowed by effective `title_capture` (fixes I-SERVER-16; touches F39, F75) (done as S-28, S-29)
- [x] Step 35: `kill` validates `pid` `integer|min:2` and that it is in the PC's latest snapshot (fixes I-SERVER-40; touches F35) (done as S-30)
- [x] Step 36: Roster header mapping uses `array_search !== false` and row-length checks (fixes I-SERVER-31; touches F54)
- [x] Step 37: Audit log records only that an exemption changed, not the reason (fixes I-SERVER-39; touches F56, F105) (done as S-35)

## Phase 7: Medium/Low bugs
- [x] Step 38: Alert acknowledge skips already-acknowledged alerts; batch IDs scoped to the lab (fixes I-SERVER-13; touches F41) (done as S-31)
- [x] Step 39: (done as S-48) Signature publish: sign first, then switch active version in a locked DB transaction; same for `activate` (fixes I-SERVER-18; touches F61, F7, F110)
- [x] Step 40: (done as S-49) `importSeed` inserts only missing rules (or `edited` flag) so dashboard edits survive bootstrap (fixes I-SERVER-19; touches F2, F60, F58)
- [x] Step 41: (done as S-39) HTML session report shows score/points as stored, adds not-observable column, matches PDF/CSV (fixes I-SERVER-20; touches F49, F51)
- [x] Step 42: `switchProfile` and `setOverride` reject non-active sessions (fixes I-SERVER-21; touches F27, F28) (done as S-22)
- [x] Step 43: (done as S-37) Retention purge covers `alerts`, `scores`, `commands`, `screen_view_audits`, `student_pc_assignments` per raw/report retention (fixes I-SERVER-15; touches F109)
- [x] Step 44: Inspector `browser_tabs` limited to the session window; score refresh moved out of the GET (fixes I-SERVER-34; touches F24, F42) (done as S-44)
- [x] Step 45: Fix `ended_at` / `started_at` `diffInSeconds` order (fixes I-SERVER-30 part; touches F44) (done as S-46)

## Phase 8: Measured performance
Each step first adds a failing-budget test (query count via `DB::listen`) or an `EXPLAIN`, then the fix.
- [x] Step 46: (done as S-47) Index `events (pc_id, type, seq)` migration, verified with `EXPLAIN` (fixes I-SERVER-28; touches F39, F24)
- [x] Step 47: Index `alerts.acknowledged_at` migration (fixes I-SERVER-41 part; touches F40) (done as S-32)
- [x] Step 48: Cache policy version per PC/session, invalidate on profile/rule/session/setting changes (fixes I-SERVER-26; touches F32, F72) (done as S-23)
- [x] Step 49: `NetworkPolicyWriter::build` resolves sessions/overrides in memory with a fixed query count (fixes I-SERVER-27; touches F5) (done as S-24)
- [x] Step 50: (done as S-40) Report controller resolves `Student::forLogin` once into a map (fixes I-SERVER-32; touches F49, F50)
- [x] Step 51: (done as S-41) Research export filters consent in SQL and streams with `lazy()` (fixes I-SERVER-33; touches F52)

## Phase 9: Dependency updates
- [ ] Step 52: Caret ranges for `barryvdh/laravel-dompdf`, `firebase/php-jwt`, `symfony/yaml` matching `composer.lock` (fixes I-SERVER-29; touches F51, F44)

## Cross-component order (server side)
Server Steps 1-3 and ccc-deploy "screen-gateway behind nginx" land before agent Steps 9-10. Server Step 6 and ccc-deploy I-CCC-6 together. Server Step 13 and analysis Step "unify export" together.

## Alternative architecture proposals (not scheduled; need owner approval)

Overall proposal: **policy as a versioned, cached, signed document, built by one `PolicyService`** (profile + session + override + settings resolved once, stored as a row keyed by `(pc_id, version)`, invalidated by events). Agents, the proxy `state.json` writer and the dashboard all read that one result. This removes I-SERVER-8/26/27 and the drift between what the settings page shows and what agents receive. Second proposal: **ingest through a validated schema** (one class per event type with allow-list, length limits and `title_capture` filter) so privacy is enforced on the server as well as the agent (I-SERVER-16/25).

| Feature(s) | Current | Better alternative | Why / cost |
|---|---|---|---|
| F1, F2, F6 Setup/bootstrap/key | Open wizard, artisan commands | Wizard locked by one-time token printed by installer; same artisan commands | Closes first-user admin takeover |
| F3 Internal CA | Lab CA key signs everything, copied to proxy | Offline root + online intermediate; separate interception CA for mitmproxy (DECISIONS #11) | Proxy compromise cannot mint agent certs |
| F4, F103 Doctor / health page | CLI + page | One `HealthChecks` class used by both (CLI check and page) | No drift |
| F5 Network-state writer | Rebuilt on web actions with per-PC queries | Event-driven writer from the policy service, atomic file, 0640 | Fewer queries, no secret in file (or env) |
| F7, F58-F61, F110 Signatures | DB rules + publish command, auto-publish each minute | Git-like YAML in `data/` as source, DB as cache; signed bundle with version, expiry; publish is a transaction | Matches the "data/ YAML" invariant |
| F10-F12 Login/roles/staff | Laravel session + roles | Keep; add lockout/throttle check and 2FA only if you ask | |
| F13, F21 Dashboard/agent channels | Reverb | Keep Reverb; origin-restricted | DECISIONS #4 |
| F14-F16, F19 Enrollment/approval | Key + MAC auto-approve | CSR + one-time per-PC token + staff approval; machine_uid required | Removes spoofable MAC trust |
| F17 Rotate key | Web action | Keep; rotate invalidates tokens | |
| F18 Rename | Web | Keep | |
| F20 Agent auth | mTLS + bearer | mTLS only with fingerprint pin; drop the bearer once enrolled, or bind it to the cert | One credential less to steal |
| F22, F23, F26 Seat layout/map/demo | Editor + live map | Keep; demo behind `APP_ENV=local` (done as S-50) | |
| F24, F25, F108 Inspector, health classification | Controller queries, dead classifier | `InspectorQueries` + `AgentHealthClassifier` with reasons | Traceable reasons invariant |
| F27, F28, F29, F30, F31, F107 Sessions/profiles/timetable | Session + profile rows, minute tick | Profile versions are immutable; sessions point to a version | Historic sessions stay explainable |
| F32, F33, F98 Policy/notice | Built on every batch | Policy service (above); notice generated from the actual enabled features | Truthful notice |
| F34 Message to students | Command with text | Keep; no free text stored beyond retention | |
| F35, F37, F38 Commands | Command rows pushed by Reverb | Command outbox with ack and expiry; validation per command type | No lost or stale commands |
| F39-F42 Alerts/score | Created in ingest, ack overwritten | Alerts append-only with review history rows | Keeps the review trail (I-SERVER-13) |
| F43, F44, F45 Screen view | Browser to :8082 with JWT | Browser to nginx `/screen-gw/` over HTTPS, JWT with `pc_id`, 30 s lease; gateway on loopback | One TLS path |
| F46 Screenshot evidence | Upload any image | Remove, or capture only inside an audited view and store hash + encrypted blob with short retention | Ethics (needs approval) |
| F49-F51 Reports | Blade HTML, PDF, CSV with different logic | One `ReportModel` DTO rendered three ways | HTML=PDF=CSV (I-SERVER-20) |
| F52, F53, F55, F56, F57 Research/rescore/consent/exempt/pseudonym | Two export implementations | Single export in server; analysis only scores; pseudonym mandatory | Ethics + less duplication |
| F54 Roster import | CSV with header mapping | Same, with a typed `RosterImport` validator and preview | Fewer silent failures |
| F71 PAC | HTTP PAC with `DIRECT` | HTTPS PAC from lab CA; deny-by-default in Test | |
| F99-F101 Downloads/install/CA | Public page with key and fingerprint | Authenticated enroll page; public page serves only the CA cert and verification instructions | |
| F102 Text share | Public board | Remove, or instructor-only | Ethics (needs approval) |
| F104 Settings | Key/value rows | Typed settings classes with defaults and validation | Fixes `full` default bug class |
| F105 Audit log | Silent failure | Hash-chained rows (each row stores previous hash) | Tamper-evident (done as S-36) |
| F106 Eval results endpoint | Internal endpoint | Keep; internal token separate | |
| F109 Retention | Partial purge | Table-driven retention registry (one place lists each table and its class) | New tables cannot be forgotten |
| F111 Scoring queue | Job calls analysis HTTP | Keep; separate analysis token; retry with backoff | Matches I-ANALYSIS-17 |

## Screen work lives in plan/screen.md
Server-side screen steps are 1-3 and 13 above (token, access, per-PC credential). The screen plan adds: call the gateway revoke endpoint on stop, logout and session end (screen S-5; done 2026-11-17, screen M2-5), send the token `mode` so wall uses thumbnails (S-13, S-37), and fetch a fresh viewer token on reconnect (S-33, S-40). Add these as server steps when the gateway side lands.
