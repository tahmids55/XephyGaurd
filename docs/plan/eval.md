# Plan: eval/ (red-team harness, load simulator, metrics export, overhead and live checks)

Sources: [audit/eval.md](../audit/eval.md) (I-EVAL-1..20), [features/eval/](../features/eval/README.md) (F119-F122).
Plan rewritten 2026-11-17 after the eval audit. Nothing run while planning. `eval/` tests the other components, so no step here changes the behaviour of what it measures; the one exception is screen benchmarking, which lives in the screen plan (Step 20) and is only wired into `eval/` here.

Rules for every step: one component, < 300 changed lines, one kind of change, test in the same step, then
`python3 -m pytest -q eval/tests` and `ruff check eval --select E,F,W --ignore E501,E731`. **[APPROVAL]** = needs your decision. **[CROSS]** = matching step in another plan.
Do not run the simulator or the red-team harness against a real lab or the production database without asking: both change state on the target (see I-EVAL-5, I-EVAL-10).

## Phase 1: Critical/High security and bugs
- [ ] Step 1: Harness writes results after every scenario and runs `cleanup` in `finally`; `ccc_results` errors are retried 3 times then recorded as "error", not fatal (fixes I-EVAL-4; touches F119)
- [ ] Step 2: Destructive scenarios (`date -s`, `/etc/hosts`, stop agent, tun device) get `destructive: true`, need `--allow-destructive`, and restore from a value saved before the change (fixes I-EVAL-5; touches F119)
- [ ] Step 3: Preflight in `run`: PC must be in an active Test session and enrolled; otherwise refuse unless `--force` (fixes I-EVAL-6; touches F119) [CROSS: server `/api/internal/eval/results` returns mode and session; F106]
- [ ] Step 4: Simulator `seq` starts from a persisted counter, and a read-back check confirms the events were stored (fixes I-EVAL-2; touches F120)
- [ ] Step 5: Simulator fails the run on error rate > 1 % and reports error count in the JSON (fixes I-EVAL-3 part 1; touches F120)

## Phase 2: Tooling
- [ ] Step 6: Ruff baseline clean for `eval/` (fixes none; touches F119-F122)
- [ ] Step 7: Fake CCC (`http.server` fixture) and tests for the harness `run` path, empty results, unknown `record` id, and the cleanup guarantee (fixes I-EVAL-17, I-EVAL-18 part 1; touches F119)
- [ ] Step 8: Tests for `metrics.detection` and `metrics.overhead` with an in-memory SQLite fixture (fixes I-EVAL-18 part 2; touches F121)
- [ ] Step 9: Test for `google_ai_overview` markers using a saved fixture page (fixes I-EVAL-16 part 1; touches F122)
- [x] Step 10: Screen benchmark entry point: `eval/harness/screen_bench.py` calls `xephyguard-screen/bench/stream_bench` and stores JSON in `eval/results/` (fixes I-EVAL-1 part 1; touches F122, F43) [CROSS: screen Step 20]
- [x] Step 11: Screen red-team scenarios (spoofed agent header, wrong `stream_id`, viewer message injection, expired lease, view of an idle PC, plaintext :8082 probe) in `scenarios/linux.yaml` (fixes I-EVAL-1 part 2; touches F119, F43) [CROSS: screen Step 23]

## Phase 3: Config and secrets
- [ ] Step 12: `--token` and `--key` read from env (`XG_INTERNAL_TOKEN`, `XG_ENROLL_KEY`) or a 0600 file; removed from the command line and from `ps` (fixes I-EVAL-8 part 1, I-EVAL-11 part 1; touches F119, F120)
- [ ] Step 13: Simulator work dir via `mkdtemp` mode 0700 (fixes I-EVAL-11 part 2; touches F120)
- [ ] Step 14: Scenario temp files use `mktemp -d` per run and are removed in cleanup (fixes I-EVAL-19; touches F119)
- [ ] Step 15: Google markers move to `data/` (shared with the server settings fallback selectors) (fixes I-EVAL-16 part 2; touches F122) [CROSS: data/, server Step 26, proxy Step 14]

## Phase 4: Structure (moves only)
No move-only items in the audit.

## Phase 5: Duplication and dead code
No separate items; `redteam.py` duplicate "any" matching is removed in Step 19.

## Phase 6: Errors, logging, validation
- [ ] Step 16: `ssh -- host`, validate `--target`/`--pc-ip` format; replace the shell-split `--rescan-cmd` default with a CCC API call (fixes I-EVAL-8 part 2; touches F119) [CROSS: server rescan endpoint, F37]
- [ ] Step 17: Store exit status and a hash of stdout, not the last 200 characters; redaction option (fixes I-EVAL-9; touches F119)
- [ ] Step 18: `agent_overhead.sh`: clear errors for a missing agent, `bc`, bad `DUR`; also sample `xephyguard-helper` PID, with an optional streaming phase (fixes I-EVAL-15, I-EVAL-1 part 3; touches F122)

## Phase 7: Medium/Low bugs
- [ ] Step 19: Remove `expect: any`; every scenario names a rule or net event; `since` uses CCC time; report skew instead of clamping TTD to 0 (fixes I-EVAL-7; touches F119) [CROSS: server time endpoint]
- [ ] Step 20: `metrics.detection` reports sessions with no score separately and takes the latest score per key (fixes I-EVAL-12; touches F121)
- [ ] Step 21: `metrics.overhead` marks estimates as estimates, uses a time series or the overhead script output, drops the 1.6 factor (fixes I-EVAL-13; touches F121, F122)
- [ ] Step 22: Metrics read the pseudonymised research export only (fixes I-EVAL-14; touches F121) [CROSS: server Step 13, analysis Step 13; APPROVAL: ETHICS.md]
- [ ] Step 23: Simulator reuses one connection per PC, enrolls in parallel, supports the one-time-token approval flow, and has `--cleanup` that retires the fake PCs; simulated PCs tagged (fixes I-EVAL-10, I-EVAL-11 part 3; touches F120) [CROSS: server Steps 8-10 change approval; do this when they land]
- [ ] Step 24: Simulator measures end-to-end latency (event time to Reverb broadcast received) (fixes I-EVAL-3 part 2; touches F120, F121)
- [ ] Step 25: Windows scenarios expanded after the Windows checklist has been run on real hardware (fixes I-EVAL-20; touches F119) [APPROVAL: needs a Windows test PC]

## Phase 8: Measured performance
- [ ] Step 26: Machine-readable (JSON) output from `agent_overhead.sh` so agent plan Step 39 and screen plan Step 22 can compare runs (fixes none; touches F122) [CROSS: agent, screen]
- [ ] Step 27: Run F120-F122 once on the dev machine at `--n 2 --minutes 1` against a local CCC and update their "unclear" status in docs/FEATURES.md with the real result or error (fixes none; touches F120, F121, F122) [APPROVAL: FEATURES.md status; ask before starting the stack on the 8 GB machine]

## Phase 9: Dependency updates
No dependency item in the audit (`eval/` reuses `analysis` packages: pandas, scikit-learn, PyYAML; bounds are tracked in the analysis plan, I-ANALYSIS-25).
