# Auto evidence snapshot on alert (F123)

What: when an alert is raised, keep evidence at once: (a) a **context snapshot** of what the CCC already holds, always;
(b) an **automatic screenshot** from the PC, opt-in per policy, off by default. Owner request 2026-11-25
("it has to be quickest"); owner chose "context + auto screenshot".

## Design (DECISIONS #32)
- **Context (server only, ~ms):** an `Alert` `created` observer (one hook for EventIngestor, NetEventsController,
  SessionManager) writes `alert_snapshots` in the same transaction: rule + reasons, latest `process_snapshot`, last
  focused window/tab titles, last 20 events of the PC in 2 min, student/session/mode, `captured_at`, sha256 of the
  canonical JSON. No new data: copies of rows the CCC already has.
- **Screenshot (fast path, < 1 s target):** the alert sends command `evidence_snapshot {nonce}` (pushed over Reverb, as
  every command) → service → helper `{"t":"evidence_snap"}` → helper grabs one frame with the existing capture,
  checks the privacy mask (masked/secure ⇒ no image, reply `masked`), encodes one full-frame JPEG with libjpeg-turbo
  (already linked; ≤ 1920 px wide, q 70) → service checks size/nonce → `POST /api/agent/v1/evidence` over mTLS :8443.
  No gateway, no viewer, no lease needed.
- **Guards:** policy `evidence.auto_screenshot` (bool, default false) + `evidence.min_severity` (default `high`);
  at most 1 auto screenshot per PC per 30 s; nonce single-use, valid 60 s, bound to the PC's cert; JPEG ≤ 4 MB
  checked by the agent and ≤ 8 MB + `getimagesize` on the server; stored like F46 (Crypt blob + sha256, same
  retention F109); every view audited; student notice gets a generated sentence when the flag is on.

## Steps (one commit each)
- [x] 1 docs: DECISIONS #32, ETHICS row, FEATURES F123 + F46 note, THREAT_MODEL row, this plan.
- [x] 2 server: `alert_snapshots` migration + model + observer + test (context only).
- [x] 3 server: policy keys + notice sentence + `evidence_snapshot` command on alert (rate limit) + PROTOCOL + tests.
- [x] 4 server: `POST /api/agent/v1/evidence` (nonce, mTLS PC, size/type) → `evidence_captures` (`alert_id`, `source`) + tests.
- [x] 5 agent core: command parse + pending nonce + helper IPC request/reply (caps) + upload + tests. PROTOCOL.
- [x] 6 agent helper: one-frame capture + mask check + full-frame JPEG (Linux, fake-capture tests; Windows: CI MinGW syntax check only, never run).
- [ ] 7 server UI: alert detail shows context + screenshot (audited view).
- [ ] 8 agent (speed): rule_hit with `alert` action triggers the helper locally, no CCC round trip; server links it.
