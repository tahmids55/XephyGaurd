# Research and evaluation

All tools live in `eval/` and write into `eval/results/<run>/` (git-ignored).

| Metric (spec §12) | Tool | Output |
|---|---|---|
| Bypass rate per layer and overall, time-to-detect | `eval/harness/redteam.py run` + `table` | `results.csv`, `bypass_table.md/.tex`, `summary.json` |
| Detection sensitivity / specificity, false-positive rate on honest sessions, ROC-AUC | `eval/export/metrics.py detection --labels labels.csv` | `detection.json` |
| Agent CPU / RAM / uplink | `eval/harness/agent_overhead.sh` (on a PC) and `metrics.py overhead` (all PCs) | JSON vs. targets (CPU < 1 %, RSS < 30 MB, < 5 MB/h) |
| Load: 100 PCs, events < 1 s | `eval/simulator/fake_agents.py --n 100` | p50/p95/max ingest latency |
| Google AI Overview still suppressed | `eval/live_checks/google_ai_overview.py` (weekly cron) | exit code |
| Research data export | dashboard *Reports → Research export*, or `python -m xg_analysis export` | pseudonymized CSV/Parquet, consenting students only |
| Results table for the paper | `eval/export/metrics.py table --out eval/results/<run>` | `results_table.md` |

## Red-team run
On the CCC, with a **Test** session active and SSH access to a prepared target PC (standard student
account, agent installed):
```bash
export XG_INTERNAL_TOKEN=$(grep XG_INTERNAL_TOKEN /opt/xephyguard/ccc-deploy/.env | cut -d= -f2)
python3 eval/harness/redteam.py run --target student@192.168.10.21 --scenarios eval/scenarios/linux.yaml --out eval/results/run1
python3 eval/harness/redteam.py record --out eval/results/run1 behavior.phone_hotspot detected   # manual scenarios
python3 eval/harness/redteam.py table --out eval/results/run1
```
Outcome per scenario: **blocked** (the attempt failed on the PC), **detected** (the CCC recorded a
matching alert or blocked network event within `--wait` seconds), **missed** (bypass). Times have
1-second resolution.

*Development smoke run (agent-only scenarios on the build machine, no gateway):* 6/6 agent scenarios
detected in < 1 s; the network scenarios were reported as bypasses, as expected without the gateway.

## Experiment mode
Sessions carry `experiment_condition` (baseline / treatment) and `experiment_group`; they are part of
every research export. Label honest vs. AI-assisted sessions according to the study protocol in a
CSV `session_id,user,label` for `metrics.py detection`.

## Python prototype comparison (optional)
For the overhead section, run an equivalent polling prototype (e.g. psutil-based) next to the agent and
measure both with `agent_overhead.sh` (pass the prototype's PID by editing the `pgrep` line).
