# Plan: screen (xephyguard-screen/ gateway, viewer, capture, core + embedded streamer)

Sources: [audit/screen.md](../audit/screen.md) (I-SCREEN-1..33), [features/screen/](../features/screen/README.md) (F43, F45, F47) and F95 (embedded streamer, filed under agent). Cross-component IDs: I-AGENT-1, I-AGENT-6, I-SERVER-1, I-SERVER-3, I-SERVER-11, I-CCC-1, I-CCC-7.
Plan rewritten 2026-11-17 after the screen audit. Revised the same day: the F95/F47 architecture is decided first (Phase 0, docs/DECISIONS.md #24, #25), and Steps 2, 16, 46 are dropped because that design deletes the code they fix. Nothing built or run while planning.

**Priority from the owner: screen view must be smooth, like Veyon.** So this plan deviates from the standard order in one place: the measured-performance work ("Smoothness", Phase 3) comes right after security and the benchmark tooling, not at the end. **Second owner requirement: the streamer must not draw noticeable power or CPU on the student PC and must work on most PCs, including CPU-only and integrated-GPU machines.** So the design rule is: cost must be proportional to what changed on screen, nothing may depend on a discrete GPU or a vendor encoder, and the streamer must throttle itself (Phase 3a). Step numbers are stable IDs; run order follows the phase listing (Phase 0 first; Phase 3a runs before Phase 3). Smoothness steps are only started once the benchmark (Step 20-22) has produced a baseline, and each one records before/after numbers.

## Targets (proposal, confirm before Phase 3)
| Mode | Target |
|---|---|
| Wall (all PCs) | 320x180 thumbnails, about 1 update/s per PC, < 10 KB each, never a full-res stream |
| Single PC live | p95 >= 15 fps while content changes, glass-to-glass latency < 250 ms on the lab LAN, first picture < 1 s, reconnect < 3 s |
| Student PC (owner requirement: low power, no extra load, works with or without a GPU) | 0 % CPU and < 2 wake-ups/s when nobody is viewing; < 8 % of one core streaming 1080p office content; works on CPU-only and integrated-GPU PCs with no discrete GPU or vendor video encoder required; below-normal priority so the student's work always wins; self-throttles to a CPU budget |
| Gateway | 100 wall streams + 2 live streams on the 8 GB CCC without dropping frames |

## Rules for every step
One component per step (tag **[screen]** = xephyguard-screen/, **[agent]** = agent/apps, **[server]**), < 300 changed lines, one kind of change, test in the same step. Commands to confirm in docs/DEVELOPMENT.md before the first code step (not in CLAUDE.md today): `cd xephyguard-screen/gateway && go vet ./... && go test -race ./...`; `cmake -S xephyguard-screen -B xephyguard-screen/build && cmake --build xephyguard-screen/build -j2 && ctest --test-dir xephyguard-screen/build --output-on-failure`; `cd xephyguard-screen && node --test viewer`. Windows capture is "compile-checked only". Any wire change updates docs/PROTOCOL.md in the same step. **[APPROVAL]** = needs your decision. **[CROSS]** = matching step in another plan.

## Phase 0: Architecture first (runs before Phase 1)
Decided so no bug fix lands in code that is about to be deleted. Agent and ccc-deploy parts are [CROSS] and update docs/PROTOCOL.md and docs/THREAT_MODEL.md in their step.
- [x] Step 43: Retire F47 standalone `xgagent` (DECISIONS #25): remove its program and CMake target only, keep `capture/` and `core/` as the shared library; update docs/FEATURES.md (fixes none; touches F47, F95)
- [x] Step 59: [agent] Helper-to-service screen channel on the existing helper IPC: message type "screen frame", length check, 4 MB cap, peer must be the session helper, bytes forwarded opaque, never decoded; tests for oversize, wrong type, wrong peer (fixes none; touches F95) [update docs/PROTOCOL.md]
- [x] Step 60: [agent] Service opens the gateway WebSocket through nginx :8443 with its mTLS cert using the realtime WSS client; relays helper messages up and `Ack`/`KeyframeReq`/`Status` down; reconnect with backoff (fixes I-SCREEN-1 part 2; touches F95) [CROSS: ccc-deploy nginx `/screen-gw/agent` location with client-cert verify and header set/strip, together with Step 1]
- [x] Step 61: [agent] Helper stops connecting to the gateway: remove its WinHTTP/WS client and `X-Agent-Token`; it only captures, encodes and writes to the service channel (fixes I-SCREEN-18, I-SCREEN-30 by removal; touches F95) (Windows part compile-checked only)
- [x] Step 62: [screen] Gateway drops the `-agent-secret` / `X-Agent-Token` path; agents authenticate only by the nginx-verified cert fingerprint (fixes I-SCREEN-1 part 2; touches F45)

## Phase 1: Critical/High security and bugs
Gateway trust and authorisation:
- [x] Step 1: [screen] Gateway stops trusting `X-Agent-Cert-Fingerprint`; accept `X-SSL-Client-Verify`/`X-SSL-Client-Cert` only from a configured loopback proxy (`-trusted-proxy`) and hash the PEM to SHA-256 (DECISIONS #24) (fixes I-SCREEN-1 part 1; touches F45) [CROSS: ccc-deploy nginx sets and strips the header]
- [x] ~~Step 2: [screen] Agent auth by short-lived per-PC JWT~~ **Dropped** (DECISIONS #24): the mTLS cert is the agent identity; replaced by Steps 60 and 62 [CROSS: tell server Step 3 and agent Step 10 that the screen JWT is not needed]
- [x] Step 3: [screen] Bind an agent connection to the stream of its cert's PC; drop frames whose header `stream_id` differs (fixes I-SCREEN-2; touches F45)
- [x] Step 4: [screen] `AuthViewer` returns `{stream_id, mode, exp, pc_id}`; reject missing/zero `stream_id` and remove the `student_id` fallback; enforce `mode` (fixes I-SCREEN-6, I-SCREEN-24; touches F43, F45)
- [x] Step 5: [screen] Close viewer sockets at lease expiry and add loopback `POST /internal/revoke` (internal token) (fixes I-SCREEN-3; touches F43, F45) [CROSS: server calls revoke on stop, logout, session end]
- [x] Step 6: [screen] Viewer to agent messages: allow only `Ack` and `KeyframeReq`, exactly 28 bytes, stream id rewritten; viewer read limit 4 KB (fixes I-SCREEN-13; touches F45)
- [x] Step 7: [screen] Origin check against configured dashboard origins, `InsecureSkipVerify` removed (fixes I-SCREEN-15; touches F45)
- [x] Step 8: (done with Step 3: the stream is fixed per connection) [screen] Fix the `stream` data race in `HandleAgent` (resolve the stream before the writer starts) (fixes I-SCREEN-19; touches F45)
- [x] Step 9: [screen] Keyframe cache: 5 s TTL, cleared on MASKED/SECURE, replayed frames flagged cached (fixes I-SCREEN-16; touches F43, F45)

Privacy and capture:
- [x] Step 10: [screen] X11 shared memory created 0600 and removed (`IPC_RMID`) right after attach (fixes I-SCREEN-4; touches F43, F47, F95)
- [x] Step 11: [agent] Wire `PrivacyFilter` into the embedded streamer (window provider from helper, rules from policy, fail-safe mask) (fixes I-SCREEN-5; touches F95, F43) [APPROVAL: ETHICS.md wording for what is masked] Rule list `data/screen/privacy_rules.yaml` + server delivery done in M2-11b.
- [x] Step 12: [screen] Viewer ordered frame queue: one frame at a time in sequence, tiles decoded in parallel, stale frames dropped, ACK on receive (fixes I-SCREEN-9; touches F43)
- [x] Step 13: [screen] Gateway sends the token's `mode` to the agent in the `Status` message (reserved field) (fixes I-SCREEN-7 part 1; touches F43, F45) [update docs/PROTOCOL.md]
- [x] Step 14: [agent] Streamer honours `wall`, `snapshot`, `live` (no hard-coded `live`) (fixes I-SCREEN-7 part 2; touches F95, F43)
- [x] Step 15: [agent] Helper starts only on a service start command (Step 59 channel) with a lease expiry, and stops itself when it lapses; the service starts it only for a CCC lease, and a gateway `KeyframeReq` alone never starts capture (fixes I-SCREEN-20; touches F95) [CROSS: agent IPC message, update PROTOCOL.md]
- [x] ~~Step 16: [agent] One mutex around the Windows WinHTTP socket~~ **Dropped** (DECISIONS #24): Step 61 removes that socket from the helper

## Phase 2: Tooling and measurement
- [x] Step 17: [screen] `go mod tidy`, `go vet`, `go test -race` as one documented command in docs/DEVELOPMENT.md (fixes I-SCREEN-32 part; touches F45)
- [x] Step 18: [screen] Remove tracked `gateway/gateway.exe` and ignore `*.exe` (fixes I-SCREEN-31; touches F45) [APPROVAL: deletes a tracked file]
- [x] Step 19: [screen] CI jobs for Go tests, viewer tests and `xgtests` (fixes I-SCREEN-32 part; touches F43, F45) [APPROVAL: CI edit] (CI jobs screen-gateway, screen-viewer, screen-core; YAML parsed and go/node commands run locally, xgtests job not run on a runner)
- [x] Step 20 (part a M3-3; part b M3-3b1..3b3): [screen] Headless benchmark `bench/stream_bench`: synthetic 1080p workloads (typing, scrolling, window drag, video-like), real encoder, real gateway, headless viewer counting decoded frames; prints fps, bytes/s, encode ms p50/p95, latency p50/p95, drops, keyframes, **CPU-seconds per streamed second and per frame, wake-ups per second, and an idle (not viewed) phase**; runs on one pinned core (`taskset -c 0`) and in a CPU-only profile (Xvfb with software rendering) as the worst case; an integrated-GPU run needs a real PC and a Windows run needs a real Windows PC, both "not run" until then (fixes none; touches F43, F45, F47)
- [x] Step 21: [screen] Gateway `/metrics` (plain text) and a viewer stats overlay (fps, latency, kbit/s) behind a debug flag (fixes I-SCREEN-25; touches F43, F45)
- [x] Step 22: [screen] Run the benchmark on the dev machine and record the baseline in docs/EVALUATION.md (numbers only, no fixes) (fixes none; touches F43)
- [x] Step 23: [eval] Screen red-team scenarios (spoofed agent header, wrong `stream_id`, viewer message injection, lease expiry, idle-PC view) in `eval/scenarios` (fixes none; touches F43, F45) [CROSS: eval plan]

## Phase 3a: Low power and CPU-only PCs (runs before Phase 3; owner requirement)
Rule: cost proportional to change. A static desktop must cost almost nothing; a busy one is capped by a budget. Nothing here may require a discrete GPU, NVENC, or any vendor library.
- [ ] Step 50 (service relay done in M4-1a/b; streamer capture-loop sleeps remain for M4-2/M4-3): [agent] Idle streamer without polling: condition variables and blocking socket reads instead of 100 ms / 250 ms / 50 ms sleep loops; nothing runs while nobody is viewing except the socket (fixes I-SCREEN-34 part 1; touches F95) (Windows part compile-checked only)
- [x] Step 51: [screen] Event-driven capture loop in `agent_state.cpp`: block on the capture's wait handle (X11 Damage fd, DXGI `AcquireNextFrame` timeout) instead of `sleep_for(10/20/30 ms)`; no work when nothing changed (fixes I-SCREEN-34 part 2; touches F43, F47)
- [x] Step 52 (M4-6a governor, M4-6b agent policy, M4-6c server setting; rectangle-size lever waits for M5-3): [screen] CPU governor: measure the capture/encode threads' own CPU time every second; if over `screen.cpu_budget_pct` (default 8) lower fps, then quality, then raise rectangle size; floor 2 fps; recover slowly; the chosen fps/quality/CPU are exposed as counters (fixes I-SCREEN-36 part 1; touches F43, F95) [CROSS: server setting + docs/PROTOCOL.md policy field]
- [x] Step 53 (M4-4; Windows not compiled locally): [agent] Capture and encode threads at idle priority (`SCHED_IDLE` / nice 19 on Linux; below-normal + background mode on Windows) (fixes I-SCREEN-36 part 2; touches F95) (Windows compile-checked only)
- [x] Step 54 (M4-5a measurement, M4-5b JPEG default + policy values; server sending them not done): [screen] Choose the codec by measurement on the pinned core and in the CPU-only profile: libjpeg-turbo (SIMD) as the default for moving regions, WebP only at low effort (method 0-1) or for static refinement; make `codec`, `quality` and WebP `method` config values; record ms per megapixel in docs/EVALUATION.md (fixes I-SCREEN-10 part 6; touches F43)
- [x] Step 55 (M4-3a wall, M4-3b safety re-read, M4-3c blanked/secure desktop; Linux lock detection needs D-Bus, owner decision): [screen] Static screens are nearly free: no capture work without Damage; wall thumbnails are re-encoded and sent only when the downscaled image changed (else a 28-byte "unchanged" message); pause while the session is locked or the display is blanked (fixes I-SCREEN-34 part 3; touches F43)
- [x] Step 56 (M4-7; not compiled locally): [screen] Windows capture: copy only dirty and move rectangles from the GPU staging texture into a persistent shadow frame, capture on the adapter that owns the output (hybrid laptops), and cap GDI fallback at 5 fps (fixes I-SCREEN-35; touches F43, F95) (compile-checked only)
- [x] Step 57 (M4-8a agent, M4-8b server): [agent] Report streaming cost (fps, CPU %, kbit/s, throttle level) in the heartbeat usage so the dashboard shows per-PC cost; numbers only, nothing from the screen (fixes none; touches F72, F95) [CROSS: server display, update docs/PROTOCOL.md and check docs/ETHICS.md]
- [x] Step 58 (M4-9; Linux Xvfb only): [screen] Acceptance run: benchmark on the pinned core and CPU-only profile shows idle < 2 wake-ups/s, live <= 8 % of one core, and the fps the governor settles at; record results (fixes none; touches F43)

## Phase 3: Smoothness (measured performance, pulled forward by owner request)
Each step quotes before/after numbers from Step 20. Stop and re-plan if two steps in a row do not improve the target metric.
- [x] Step 24 (M5-1): [screen] Pacing: remove the 200 ms floor; target frame interval from configured fps (default 15 live, 1 wall); in-flight window in bytes, not 3 frames; coalesce updates instead of waiting (fixes I-SCREEN-8; touches F43)
- [x] Step 25 (M5-2): [screen] Per-viewer catch-up: new or lagging viewer gets the cached keyframe; agent keyframe requests rate-limited to 1/s (fixes I-SCREEN-12; touches F43, F45)
- [x] Step 26: [screen] Protocol v2 rectangle records (`x,y,w,h` in pixels) in core with v1 decode kept, fuzz and golden tests (fixes I-SCREEN-10 part 1; touches F43, F47) [update docs/PROTOCOL.md; keep v1 until viewers are updated]
- [x] Step 27: [screen] Viewer parses and draws v2 rectangles (fixes I-SCREEN-10 part 2; touches F43)
- [x] Step 28 (opt-in `EncoderConfig.rectangles`, default off until the dashboard viewer reads v2; not benchmarked): [screen] Encoder merges dirty tiles into rectangles (max 256x256) and encodes one image per rectangle (fixes I-SCREEN-10 part 3; touches F43, F47)
- [x] Step 29 (M5-4a pool + reused state in `FrameEncoder`, M5-4b governor-driven 2nd worker, M5-4c capture/encode overlap; not benchmarked): [screen] Encoder worker pool (1 thread by default, 2 only while the CPU governor has headroom; no new dependency), reused WebP/JPEG state, capture and encode overlap (fixes I-SCREEN-10 part 4; touches F43)
- [x] Step 30 (compile-checked only): [screen] Progressive refinement: fast lossy first, then re-send static regions at high quality so text becomes sharp, only when the governor has spare budget (fixes I-SCREEN-10 part 5; touches F43)
- [x] Step 31: [screen] X11 capture uses Damage rectangles, skips capture when none, drops the per-pixel alpha loop (fixes I-SCREEN-11; touches F43, F47, F95)
- [x] Step 32 (head index only, not run): [screen] Viewer draw path: index loop instead of `shift()`, one `OffscreenCanvas` worker if the benchmark shows main-thread jank (fixes I-SCREEN-27; touches F43)
- [x] Step 33 (not run): [screen] Viewer auto-reconnect with backoff; page supplies a fresh token through a callback (fixes I-SCREEN-17; touches F43)
- [x] Step 34 (built, not run): [agent] Cursor position at up to 30 Hz from the helper, drawn on a viewer overlay (fixes I-SCREEN-23; touches F95, F43) [update docs/PROTOCOL.md if fields change]
- [x] Step 35 (M5-9): [screen] Raise `kMaxTiles` limit per message and send per-monitor frames with `monitor_id`; viewer monitor selector (fixes I-SCREEN-21, I-SCREEN-22 part 1; touches F43)
- [x] Step 36 (M5-9, not compiled): [screen] Windows DXGI: capture every output (fixes I-SCREEN-22 part 2; touches F43, F95) (compile-checked only)
- [x] Step 37 (M5-10): [server] Dashboard wall uses real `mode=wall` streams instead of the synthetic drawer (fixes none; touches F43, F26) [CROSS: server plan; APPROVAL: F26 demo stays available]
- [ ] Step 38: [screen] Re-run benchmark; record results against the targets above and decide the "H.264 gate" below (fixes none; touches F43)

## Phase 4: Config and secrets
- [x] Step 39 (vet only): [screen] Gateway listens on `127.0.0.1:8082` by default; `-dev-insecure` refuses non-loopback binds (fixes none, supports I-CCC-1; touches F45) [CROSS: ccc-deploy Step 1-2]
- [x] Step 40 (M6-2): [screen] Viewer token sent as the first WebSocket message instead of the URL query (fixes I-SCREEN-29; touches F43, F45) [CROSS: server page code]
- [x] Step 41 (configure checked): [screen] Pin libwebp / libjpeg-turbo by commit hash and document the CMake patch (fixes I-SCREEN-33; touches F47)

## Phase 5: Structure (moves only)
- [x] Step 42: [screen] Split `gateway/relay.go` into `stream.go`, `agent_handler.go`, `viewer_handler.go` (move only) before Phase 3 gateway changes if the diff gets large (fixes none; touches F45)

## Phase 6: Duplication and dead code
- Step 43 moved to Phase 0 (decided: retire F47, DECISIONS #25).

## Phase 7: Errors, logging, validation
- [x] Step 44 (vet only): [screen] Gateway `ReadHeaderTimeout`, WebSocket ping/pong every 10 s, "agent offline" status on timeout (fixes I-SCREEN-14; touches F45)
- [x] Step 45 (viewer tests pass): [screen] Viewer reports decode and control errors through `onError` and counters (fixes I-SCREEN-28; touches F43)
- [x] ~~Step 46: [agent] Windows receive loop handles fragmented messages~~ **Dropped** (DECISIONS #24): Step 61 removes that loop; the service's realtime WSS client must already handle fragments with a cap (check in Step 60)

## Phase 8: Medium/Low bugs
- [x] Step 47: [screen] Remove idle streams from the map after 60 s (fixes I-SCREEN-26; touches F45)
- [ ] Step 48: Wayland: owner decision between forcing Xorg on lab PCs and a PipeWire portal path (no audit ID; touches F43) [APPROVAL; installers out of scope here]

## Phase 9: Dependency updates
- [x] Step 49 (report in screen-implementation.md M6-8): [screen] Review `coder/websocket`, `golang-jwt/jwt`, libwebp, libjpeg-turbo versions; report first, update only with approval (fixes none; touches F45, F47)

## Phase gate: H.264 (only if Step 38 misses the targets)
Software H.264 at 1080p costs a large share of a core even for a static desktop, which breaks the low-power rule, so it is **not** an option for CPU-only PCs. A video path is considered only as an optional, hardware-accelerated mode (VAAPI on Linux, Media Foundation on Windows, which covers Intel, AMD and NVIDIA through one API) that is probed at start, reported in `screen_caps`, and falls back to the tile path when absent. Consider it only if Step 58 shows the tile path cannot reach p95 15 fps on scrolling or video-like content within the CPU budget. It needs a new dependency decision, so it is yours to approve, not a scheduled step.

## Alternative architecture proposals (not scheduled; need owner approval)

Reference implementation to match: Veyon (VNC/RFB underneath, with thumbnails for monitoring and a full view for a single PC). We copy what makes it feel smooth, not its remote-control feature: update rectangles (not fixed 64 px tiles), several updates in flight with the sender never waiting for the viewer's paint, a separate cheap thumbnail path, and a visible cursor.

| Option | What | Smoothness | Cost / risk | Verdict |
|---|---|---|---|---|
| A. Improve the current design (this plan) | Keep WebSocket + gateway; damage-driven capture, protocol v2 rectangles, SIMD JPEG, CPU governor, idle priority, progressive text refinement, ordered viewer | 15+ fps for office-style content at under 8 % of one core; scrolling/video may be capped by the governor | No new dependency; works on CPU-only and integrated-GPU PCs; incremental; every step measured | **Do now** |
| B. H.264 video path (WebCodecs in the browser) | Hardware/software encode on the PC, viewer decodes with `VideoDecoder` | Best for motion, bandwidth drops a lot; 30 fps easy | Software encode is too expensive for CPU-only PCs (x264 is also GPL); hardware encode differs per OS and GPU, so a tile fallback is still required; text softness at low bitrate | Optional hardware-only mode behind the gate after Step 58 |
| C. WebRTC (DTLS-SRTP) | Browser connects with RTCPeerConnection; gateway as an SFU (pion in Go) | Built-in congestion control and jitter handling | Agent side needs a C++ WebRTC library (libdatachannel); STUN/ICE complexity on a lab LAN; heavy | Not recommended |
| D. RFB/VNC (libvncserver + noVNC) | Same family as Veyon | Proven encodings (Tight, ZRLE), mature | New dependency and a second protocol; view-only enforcement and privacy mask must be rebuilt; plain RFB needs a TLS tunnel | Not recommended |

| Feature | Current | Better alternative | Why |
|---|---|---|---|
| F43 Live screen view | Browser to :8082 over WS with single-use token | Browser to nginx `/screen-gw/` over HTTPS, mode-bound lease token, reconnect with a fresh token, stats overlay | One TLS path, survives blips, measurable |
| F45 Gateway relay | Shared secrets, no revoke, no keepalive, forwards everything | Loopback-only behind nginx; per-PC JWT for agents; revoke endpoint; allow-listed messages; `/metrics` | Closes I-SCREEN-1..3, 13..15 |
| F47 Standalone xgagent | Separate screen-only agent | **Decided (DECISIONS #25):** retire in favour of the embedded streamer, keep `capture/` and `core/` as one shared library with the benchmark as its test | One implementation of capture/encode |
| F95 Embedded streamer | Helper streams directly to the gateway with a token it can read | **Decided (DECISIONS #24):** helper captures; root service relays opaque frames to the gateway over its own mTLS WebSocket; start/stop by CCC lease from the service (Phase 0) | No credential in user space; no self-start (I-SCREEN-20) |
| Wall (part of F43) | Always live streams | `mode=wall` thumbnails (JPEG, 1 fps, 320 px), gateway caches the latest per PC | Scales to a full lab |
