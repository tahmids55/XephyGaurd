# Research and evaluation

All tools live in `eval/` and write into `eval/results/<run>/` (git-ignored).

| Metric (spec §12) | Tool | Output |
|---|---|---|
| Bypass rate per layer and overall, time-to-detect | `eval/harness/redteam.py run` + `table` | `results.csv`, `bypass_table.md/.tex`, `summary.json` |
| Detection sensitivity / specificity, false-positive rate on honest sessions, ROC-AUC | `eval/export/metrics.py detection --labels labels.csv` | `detection.json` |
| Agent CPU / RAM / uplink | `eval/harness/agent_overhead.sh` (on a PC) and `metrics.py overhead` (all PCs) | JSON vs. targets (CPU < 1 %, RSS < 30 MB, < 5 MB/h) |
| Load: 100 PCs, events < 1 s | `eval/simulator/fake_agents.py --n 100` | p50/p95/max ingest latency |
| Google AI Overview still suppressed | `eval/live_checks/google_ai_overview.py` (weekly cron) | exit code |
| Research data export | dashboard *Reports → Research export* | pseudonymized CSV, consenting students only (convert to Parquet with pandas if needed) |
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
The `screen.*` scenarios attack the screen gateway from the PC and need `--ccc <host the PC reaches the CCC at>` (skipped without it); they raise no CCC
alert by design (`expect: blocked_only`), so their outcome is blocked or missed. The checked-in gateway tests cover the ones that need a token or certificate.
Not run against a real stack yet: the curl and JWT parts were only tried against a local gateway (forged token 401, correctly signed 101).

*Development smoke run (agent-only scenarios on the build machine, no gateway):* 6/6 agent scenarios
detected in < 1 s; the network scenarios were reported as bypasses, as expected without the gateway.

## Experiment mode
Sessions carry `experiment_condition` (baseline / treatment) and `experiment_group`; they are part of
every research export. Label honest vs. AI-assisted sessions according to the study protocol in a
CSV `session_id,user,label` for `metrics.py detection`.

## Python prototype comparison (optional)
For the overhead section, run an equivalent polling prototype (e.g. psutil-based) next to the agent and
measure both with `agent_overhead.sh` (pass the prototype's PID by editing the `pgrep` line).

## Screen streaming baseline (before any performance step)
Numbers only, no fixes; later steps (docs/plan/screen.md Phases 3a and 3) quote these as "before". Recorded 2026-11-18 (screen Step 22).
Setup: Intel i3-1220P (12 threads), Linux 6.8, **Release** build (`-O3`) of `xephyguard-screen` (an earlier Debug-build run was discarded),
`python3 eval/harness/screen_bench.py --seconds 10 [--profile xvfb]`: streamer pinned to CPU 0 (`taskset -c 0`), real gateway on loopback
(`-dev-insecure`), headless viewer, 1920x1080, one 10 s run per workload. Latency = capture return to viewer receipt through the gateway
(tile diff + encode + relay); it excludes browser decode and paint. The viewer ACKs each frame. Drops = frames sent minus frames received: 0 in every run.
CPU % is the streamer's own share of one core. Native = synthetic capture (full copy per call like the X11 capture). Xvfb = real X11 capture
(SHM + Damage) on Xvfb with software rendering, the CPU-only worst case; a forked painter draws the workload.

| Workload | Profile | fps | kB/s | Latency p50 / p95 ms | CPU % of a core | Wake-ups/s |
|---|---|---|---|---|---|---|
| static | native | 0.1 | 18 | 139 / 139 | 23.1 | 86 |
| static | Xvfb | 0.2 | 22 | 142 / 142 | 21.3 | 138 |
| typing | native | 4.9 | 37 | 2.9 / 4.3 | 4.5 | 100 |
| typing | Xvfb | 4.9 | 25 | 6.2 / 10.9 | 4.6 | 102 |
| scrolling | native | 3.8 | 531 | 130 / 141 | 48.3 | 61 |
| scrolling | Xvfb | 3.6 | 393 | 140 / 157 | 44.7 | 60 |
| window drag | native | 4.8 | 29 | 5.3 / 10.3 | 4.5 | 99 |
| window drag | Xvfb | 0.8 | 20 | 11 / 168 | 20.7 | 127 |
| video-like | native | 3.9 | 1572 | 119 / 133 | 46.0 | 80 |
| video-like | Xvfb | 3.7 | 1454 | 138 / 149 | 43.2 | 77 |

Reading it against the targets (docs/plan/screen.md, not yet met):
- **Smoothness:** 3.6 to 4.9 fps on every moving workload, far below p95 >= 15 fps. The ceiling is the gateway/agent pacing (Step 24), not the CPU: typing and drag use 4.5 % of a core.
- **Static screen:** about 21 to 23 % of a core and 86 to 138 wake-ups/s with nothing changing, against a target of ~0 % and < 2 wake-ups/s. The capture runs a full copy and diff on a timer (Steps 50, 51, 55).
- **Heavy motion (scrolling, video-like):** 43 to 48 % of a core, against the 8 % budget (Steps 52, 54, 26 to 29).
- **Idle (nobody viewing):** 0.0 to 0.1 % CPU. The 10 wake-ups/s in the idle row come from the bench's own stdin poll in `--stdio` mode; plain `xgstreambench --workload idle` shows 0.2/s.
- **Xvfb drag:** 0.8 fps is limited by the painter pushing a full 1080p image to Xvfb, not by the streamer; treat that cell as a bench limit.
- Not measured: integrated-GPU PC, real Windows capture, browser decode and paint lag (use the viewer `?debug=1` overlay), several viewers or PCs at once.

### Screen streaming: before/after per low-power step
Same setup as the baseline above (Release, `taskset -c 0`, Xvfb profile = real X11 capture, 10 s per workload, `xgstreambench --capture x11`, no gateway). Dev machine, 2026-11-18.

| Step | static CPU % | static wake-ups/s | typing CPU % | scrolling CPU % | notes |
|---|---|---|---|---|---|
| baseline (full copy + hash every 20 ms) | 21.3 | 138 | 4.6 | 44.7 | 47 captures/s of an unchanged screen |
| M4-2a Damage events, damaged rectangles only (loop still sleeps 20 ms) | 3.2 | 50 | 5.6 | 44.8 | 0.66 ms per empty capture |
| M4-2b capture loop blocks in `wait_for_change` (250 ms bound for the privacy check, 1 s safety re-read) | 2.7 | 7.4 | 5.1 | 44.0 | the 2.7 % is the 1 Hz safety re-read (8 MB read + hash), next in M4-3 |
| M4-3b safety re-read compares with the shadow, reports only changed rows (none: NoChange, no copy, no hash) | 0.9 (30 s run; 1.2 before) | 5.5 (6.5 before) | 5.3 | - | 10 s rows include the one first keyframe (140 ms = 1.4 % of 10 s); 30 s run without it: about 0.7 % -> 0.45 % |
| M4-4 capture/encode thread at `SCHED_IDLE` | - | - | 4.4 | - | typing 5 s on core 0: 4.8 fps alone; with a busy loop on the same core 0.2 fps (the stream yields to student work; Windows background mode not run) |

#### M4-6a CPU governor (budget 8 % of one core, floor 2 fps)
Same setup, 10 s per workload, governor numbers from `xgstreambench` (`gov fps`, `q`, `lvl`).

| codec | scrolling before -> after | video before -> after | notes |
|---|---|---|---|
| JPEG q85 | 10.6 % at 5 fps -> 6.7 % at 3.1 fps | 10.9 % at 5 fps -> 7.0 % at 3.2 fps | settles under budget; typing (1.8 %) is not throttled |
| WebP q85 method 4 | 48.2 % at 3.6 fps -> 29.7 % at 2.2 fps, quality 0.5 | 45.6 % -> 25.5 % at 2.2 fps | the 2 fps floor wins: WebP cannot meet 8 % on moving content |

#### M4-5 codec by measurement
Synthetic 1080p capture, Release, `taskset -c 0`, 5 s per workload, `xgstreambench --codec ... [--quality N] [--webp-method N]`, 2026-11-18. All rows run at the 5 fps pacing cap (200 ms floor, M5-1), so CPU compares the codecs at the same frame rate. ms/MP = build p50 / 2.07 MP (scrolling and video change nearly every tile).

| codec | typing CPU % / B/s | scrolling CPU % / B/s | video CPU % / B/s | ms per MP (scroll, video) |
|---|---|---|---|---|
| WebP q85 method 4 (old default) | 4.7 / 38 k | 48.2 / 583 k (3.6 fps) | 45.6 / 1.57 M (3.8 fps) | 65, 57 |
| WebP q85 method 1 | 3.5 / 60 k | 43.1 / 1.22 M | 35.2 / 2.07 M | 40, 33 |
| WebP q85 method 0 | 3.5 / 60 k | 45.2 / 1.18 M | 38.1 / 2.07 M | 44, 36 |
| JPEG q85 (libjpeg-turbo, 4:2:0, fast DCT) | 2.1 / 190 k | 10.6 / 4.26 M | 10.9 / 2.74 M | 9.0, 10.0 |
| JPEG q70 | 2.0 / 164 k | 9.1 / 3.78 M | 8.9 / 1.95 M | 8.3, 8.0 |
| JPEG q60 | 2.0 / 154 k | 9.6 / 3.57 M | 9.1 / 1.70 M | 8.7, 7.9 |

JPEG costs 4-6x less CPU than WebP on moving content; WebP at low effort saves about 25 % CPU over method 4 but doubles the bytes and stays 3-4x JPEG. The synthetic scrolling pattern is noise-like, so its byte counts are a worst case. Not measured: real desktop content, integrated-GPU PC, Windows.

#### M4-9 acceptance run (Phase 3a low-power targets)
Release, `taskset -c 0`, `xgstreambench --capture x11` on Xvfb 1920x1080 (real X11 capture, software rendering = the CPU-only profile),
production defaults (JPEG q85, governor budget 8 %, floor 2 fps), 10 s per moving workload, 30 s static and idle, dev machine 2026-11-18.

| Workload | fps | kB/s | CPU % of a core | Wake-ups/s | governor fps / q / level | target |
|---|---|---|---|---|---|---|
| idle (nobody viewing) | 0 | 0 | 0.0 | 0.0 | - | < 2 wake-ups/s: **met** |
| static (viewed) | 0.07 | 42 | 0.3 | 5.5 | 15 / 1.00 / 0 | near 0 %: met (5.5/s = 4 safety re-reads + privacy check) |
| typing | 5.0 | 141 | 4.0 | 88 | 15 / 1.00 / 0 | <= 8 %: met, not throttled |
| scrolling | 3.4 | 2552 | 7.3 | 58 | 2.7 / 1.00 / 13 | <= 8 %: **met**, settles at 2.7 fps |
| video-like | 3.2 | 1728 | 7.4 | 52 | 3.2 / 1.00 / 12 | <= 8 %: **met**, settles at 3.2 fps |

Same run with WebP q85 method 4 (not the default): scrolling 28.6 % and video 24.6 % at the 2 fps floor, quality 0.5; WebP cannot meet the budget on moving content. Not measured: real Windows capture (M4-7 not compiled locally), integrated-GPU PC, real desktop content. Smoothness (>= 15 fps) is Phase 3 (M5).

#### M5-1 pacing without the 200 ms floor, byte window (Step 24)
Same setup as M4-9 (Xvfb, `taskset -c 0`, JPEG q85, 10 s). Window: 8 frames / 2 MiB unacknowledged, lost frames expire after 2 s.

| Workload | fps before -> after | CPU % before -> after | notes |
|---|---|---|---|
| typing | 5.0 -> 8.3 | 4.0 -> 6.2 | the 200 ms floor was the cap; synthetic typing 4.9 -> 5.9 fps |
| scrolling | 3.4 -> 4.5 | 7.3 -> 9.3 | governor settles at 3.6 fps; 10 s includes its descent from 15 fps |
| video-like | 3.2 -> 3.9 | 7.4 -> 9.7 | same |
| drag (Xvfb) | 0.5 -> 1.3 | 22.6 -> 22.5 | cost is 49 captures/s of the painter's full-screen puts, not the pacing (bench limit, see baseline) |

Moving content is unchanged (scrolling, video-like stay near 44 %): its cost is the encoder, not the capture (M4-5, M5-3, M5-4).

#### M5-4a encoder worker pool and reused codec state (Step 29)
`FrameEncoder` keeps a codec context per thread (TurboJPEG handle, output buffer, pixel buffer, WebP config) and can encode one frame's rectangles on `EncoderConfig.workers` threads (default 1; output byte-identical for any count, tested). Helper threads run at idle priority; their CPU is `worker_cpu_seconds()` (the governor does not add it yet: M5-4b). Synthetic 1080p scrolling, JPEG q85, `--rects`, 5 s, Release, `taskset -c 0-3`:

| workers | fps | build p50 | CPU (all threads) |
|---|---|---|---|
| 1 | 4.2 | 45.0 ms | 46.0 ms/frame |
| 2 | 5.0 | 27.2 ms | 50.8 ms/frame |

Pinned to 2 cores that also run the bench workload (`taskset -c 0,1`) 2 workers gave no gain (build p50 41.7 -> 42.1 ms) and 1.8x CPU: idle-priority helpers only help when a core is really free, so the agent must switch to 2 only while the governor has headroom (M5-4b). Per-frame allocation savings of the reused state are within noise (encode time dominates).
