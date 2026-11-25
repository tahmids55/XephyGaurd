# F27. Start, stop and switch a session

- **Component:** server (also: agent)
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff start a lab session with a profile (Practice/Test/Exam), stop it, or switch the
  profile mid-session. All PCs get the new policy within seconds.
- **Trigger:** `GET /sessions` (+ `.html`), `POST /sessions`, `POST /sessions/{session}/stop`,
  `POST /sessions/{session}/profile`. Assistant and up.
- **Code:** `S/Http/Controllers/Web/SessionController.php`, `S/Services/SessionManager.php`
  (`start`, `stop`, `switchProfile`), `PolicyBuilder`.
- **In/out/side effects:** writes `lab_sessions`, `session_pcs`; sends `policy_updated` to PCs;
  writes net state; stopping queues scoring ([F48](../analysis/F048-session-scoring-v1-rules-v2-isolation-forest.md)).
- **Status:** working (`test_session_blocks_and_ending_relaxes`, `practice_block_profile_uses_block_ai_group`).
- **Test:** 1. Start "Test: AI blocked". 2. On a lab PC `chatgpt.com` is blocked. 3. Stop: it loads again.
