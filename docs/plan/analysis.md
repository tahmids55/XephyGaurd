# Plan: analysis/ (v1 rules, v2 Isolation Forest, research export, HTTP service)

Sources: [audit/analysis.md](../audit/analysis.md) (I-ANALYSIS-1..26), [features/analysis/](../features/analysis/README.md) (F9, F48; F52/F53 shared with server).
Plan written 2026-11-17. Nothing run while planning.

Rules for every step: one component, <300 changed lines, one kind of change, test in the same step, then
`python3 -m pytest -q analysis/tests` and `ruff check analysis --select E,F,W --ignore E501,E731`.
**[APPROVAL]** = changes a documented feature; **[CROSS]** = another component's plan has the matching step. Every score keeps traceable reasons (invariant).

## Phase 1: Critical/High security and bugs
- [ ] Step 1: Characterization tests for `service.py` (token missing/wrong, 404, 422, body size, `/health`) so later fixes are safe (fixes I-ANALYSIS-26; touches F9)
- [ ] Step 2: Bind to `XG_ANALYSIS_BIND` default `127.0.0.1` (fixes I-ANALYSIS-1; touches F9) [CROSS: ccc-deploy compose sets it explicitly, I-CCC-18]
- [ ] Step 3: Skip v2 scoring (no stored v2 score) for exempt students (fixes I-ANALYSIS-2; touches F48, F56)
- [ ] Step 4: Duplicate `students.login` no longer raises; ambiguous login is reported as a result with a reason, never a 422 for the whole session (fixes I-ANALYSIS-3; touches F48)
- [x] Step 5: Research export pseudonym handling is a server fix (Server Step 13); here only add a test that the CLI export never includes `student_number` (fixes I-ANALYSIS-4 part; touches F52) [CROSS]
  Done 2026-11-18: `test_research_export_never_has_student_number_name_or_login` (CSV and Parquet: no `student_number`/`name`/`login`/`student_id`/`user` column, no such value in any cell). Server part was S-41. Passes on current code. 18 tests pass; ruff not installed locally, not run.

## Phase 2: Tooling
- [ ] Step 6: Ruff baseline clean for `analysis/` (no behavior change) (fixes none; touches F9, F48)
- [ ] Step 7: Schema test that `FEATURES` equals the features computed and stored (will fail until Step 20) (fixes none; touches F48)

## Phase 3: Config and secrets
- [ ] Step 8: Separate `XG_ANALYSIS_TOKEN` for the scoring endpoint, not `XG_INTERNAL_TOKEN` (fixes I-ANALYSIS-17; touches F9) [CROSS: server `AnalysisClient`, ccc-deploy env]
- [ ] Step 9: No silent SQLite fallback: missing `XG_DB_URL` is a startup error unless `XG_ALLOW_SQLITE=1` (tests set it) (fixes I-ANALYSIS-23; touches F9)
- [ ] Step 10: IDE/terminal app allow-list and AI category set loaded from `data/` YAML (fixes I-ANALYSIS-21; touches F48) [CROSS: data/]

## Phase 4: Structure (moves only)
No move-only items in the audit.

## Phase 5: Duplication and dead code
- [ ] Step 11: Remove `alert_titles`, `categories`, `BURST_WINDOW_S`, `ScoreResult.features`, unused `sp.student_id`, `source_app`, `target_app` loads (fixes I-ANALYSIS-24; touches F48)
- [ ] Step 12: Use `IDLE_GAP_S` in the reason label (fixes I-ANALYSIS-19; touches F48)
- [x] Step 13: One research export implementation: the CLI calls the same column list and pseudonym rule as the server, or the CLI is removed (decide) (fixes I-ANALYSIS-22; touches F52) [APPROVAL; CROSS: server Step 13]
  Done 2026-11-18: decided **remove** (DECISIONS #30). `export.py` and the `export` subcommand are gone; the server `ResearchExport` (S-41) is the only export. Reason: the CLI had no role check and no audit row, and auditing from Python would duplicate the audit hash chain. Lost: Parquet output (convert the CSV). Test change (needs your OK): the two CLI export tests (incl. Step 5's) are deleted with the code; their checks (consent order, withdrawal, no number/name/login) live in server `ResearchExportTest`. New `test_cli_has_no_research_export`. pyarrow now unused (Step 27). 17 tests pass; ruff not run (not installed).

## Phase 6: Errors, logging, validation
- [ ] Step 14: `ThreadingHTTPServer` handler `timeout`, check token before reading body, negative `Content-Length` rejected (fixes I-ANALYSIS-5, I-ANALYSIS-15; touches F9)
- [ ] Step 15: `/health` reuses one connection (pool) and closes it; rate-limited (fixes I-ANALYSIS-6; touches F9)
- [ ] Step 16: Distinguish `KeyError` for missing `session_id` (422) from internal errors (500) (fixes I-ANALYSIS-14; touches F9)
- [ ] Step 17: 500 responses return a generic message plus an error id; details only in the log (fixes I-ANALYSIS-16; touches F9)
- [ ] Step 18: `load_session` selects only needed student columns, never the real `name` (fixes I-ANALYSIS-20; touches F48) [update ETHICS.md note]

## Phase 7: Medium/Low bugs
- [ ] Step 19: Late large paste is not halved/labelled "early": compare the late paste, not the early total (fixes I-ANALYSIS-8; touches F48)
- [ ] Step 20: Add `net_flags` to `FEATURES` so stored features and export match scored values (fixes I-ANALYSIS-18; touches F48, F52)
- [ ] Step 21: Count a code insertion once in `idle_then_burst` (paste or folder growth, not both); update the "bob" fixture expectation with the reason text (fixes I-ANALYSIS-9; touches F48)
- [ ] Step 22: Include alerts with `user` NULL for the PC when a user filter is applied (fixes I-ANALYSIS-10; touches F48)
- [ ] Step 23: `blocked_attempts` counted per user when several logins share a PC (fixes I-ANALYSIS-12; touches F48)
- [ ] Step 24: Match students by seat assignment `sp.student_id` first, then login; shared/generic logins are flagged (fixes I-ANALYSIS-13; touches F48)
- [ ] Step 25: v2: score relative to a stored cohort baseline (not min-max per session); keep reasons for any score above threshold (fixes I-ANALYSIS-7; touches F48) [APPROVAL: changes scoring behavior]

## Phase 8: Measured performance
- [ ] Step 26: Build a 20-session fixture, time `baseline()` against the 120 s `AnalysisClient` timeout; then cache session features and load the `students` table once (fixes I-ANALYSIS-11; touches F48, F53)

## Phase 9: Dependency updates
- [ ] Step 27: Upper bounds (`<next major`) in `requirements.txt`; hashes only if you approve a lock file (fixes I-ANALYSIS-25; touches F9, F48)

