# XephyGuard Screen View: State & Progress

## WP0: Core Library (T0 - T4) - COMPLETE & VERIFIED

### What exists now
- **Fixed Contracts:**
  - `core/protocol.h`: Little-endian binary wire protocol with 28-byte Message header and 10-byte tile header (`magic = 0x5847`, `version = 1`, `kTile = 64`).
  - `capture/capture.h`: Abstract `ScreenCapture` interface and `Frame` struct with dirty rectangles.
  - `core/tiles.h`: `TileTracker` tracking changed 64x64 tiles with xxHash64 and pending tile accumulator.
  - `core/encoder.h`: `FrameEncoder` supporting WebP and JPEG tile encoding and adaptive full-frame fallback.
  - `core/flow.h`: `FlowControl` with max 3 in-flight constraint, EWMA RTT estimation, and dynamic quality scaling (0.3..1.0).
- **Core Implementations:**
  - `core/protocol.cpp`: `encode` and `decode` with bounds checking and fuzz resistance.
  - `core/tiles.cpp`: xxHash64 row-by-row tile hashing respecting stride and partial edge tiles.
  - `core/encoder.cpp`: TurboJPEG and WebP encoding for cropped tiles with quality scaling.
  - `core/flow.cpp`: Pure injected time logic (`now_ms`) for in-flight limiting, RTT smoothing, and quality adjustment.
  - `capture/fake_capture.h` / `fake_capture.cpp`: Deterministic synthetic frame generator.
- **Dependencies (`third_party/` & `FetchContent`):**
  - `third_party/doctest.h`: v2.4.11
  - `third_party/xxhash.h` & `third_party/xxhash.c`: v0.8.2
  - `libwebp`: v1.4.0 (fetched and statically linked)
  - `libjpeg-turbo`: v3.0.3 (fetched and statically linked, patched for subdirectory integration)
- **Test Suite (`xgtests`):**
  - `tests/test_protocol.cpp`: Roundtrip (0, 1, 100 tiles), error rejection on malformed inputs, 10,000 random buffer fuzz test, mutation bit-flip & truncation tests.
  - `tests/test_tiles.cpp`: Identical frame zero changes, single pixel changes, clean dirty rects, full dirty rehash, odd resolutions (1366x768).
  - `tests/test_encoder.cpp`: WebP & JPEG PSNR > 30 dB verification against decoded source, full-frame threshold triggering, keyframe/delta flag verification.
  - `tests/test_flow.cpp`: Max 3 in-flight restriction, target interval clamping (200..1000 ms), quality degradation on packet pressure, quality recovery after 5 clean acks.

### Decisions made
1. `libjpeg-turbo` integration: Handled `add_subdirectory` check cleanly via automated CMake file patching during configuration.
2. FlowControl testing: Simulated RTT and packet pacing tested with synthetic timestamps.
3. Edge tiles: Explicitly clamped tile dimensions at screen edges so non-multiple-of-64 resolutions (e.g. 1366x768) are accurately encoded without out-of-bounds stride accesses.

### Status
- Build: GCC 14.2.0 (MinGW) / C++17 with `-Wall -Wextra -Wpedantic` clean (0 warnings in core).
- All 18 test cases and 955 assertions passing (100% green).
- `ctest` passes 100%.


## WP1: In-process Pipeline (T5) - Milestone M1 - COMPLETE & VERIFIED

### What exists now
- `tests/test_pipeline.cpp`: Complete end-to-end in-process integration test connecting `FakeCapture` -> `TileTracker` -> `FrameEncoder` -> `encode` -> `decode` -> `ViewerState`.
- `tests/fixtures/keyframe.bin`: Golden binary serialized keyframe fixture.
- `tests/fixtures/delta.bin`: Golden binary serialized delta frame fixture.
- `tests/fixtures/full.bin`: Golden binary serialized full frame fixture.

### Tests & Verification
1. `Pipeline: End-to-end capture to viewer with PSNR > 30 dB`:
   - Runs 10 consecutive frames through capture, tracking, encoding, wire roundtrip, and viewer decode/blit.
   - Evaluates PSNR against source frame after every frame; all frames exceed 30 dB.
2. `Pipeline: Intermediate frame drop triggers base_seq mismatch and keyframe recovery`:
   - Frame 1 delivered as KEY (seq 1, base 0).
   - Frame 2 DELTA (seq 2, base 1) dropped.
   - Frame 3 DELTA (seq 3, base 2) received: viewer detects mismatch (`base_seq 2 != last_applied 1`) and requests keyframe.
   - Sender resets flow and forces all tiles pending.
   - Frame 4 delivered as KEY (seq 4, base 0): viewer recovers cleanly, resetting state and achieving PSNR >= 30 dB.
3. `Pipeline: Dump fixtures keyframe.bin, delta.bin, full.bin`:
   - Dumps standard fixtures required by WP2 browser viewer tests into `tests/fixtures/`.

### Status
- Build: GCC 14.2.0 (MinGW) / C++17 clean (0 warnings).
- Test suite: 21 test cases, 1026 assertions, 100% passing.
- `ctest` runs 1/1 tests passed (100%).


## WP2: Gateway Relay and Browser Viewer (T6 Relay, T7) - COMPLETE & VERIFIED

### What exists now
- **Gateway (`gateway/`):**
  - `gateway/auth_iface.go`: Fixed contract defining `Authenticator` interface (`AuthViewer` and `AuthAgent`).
  - `gateway/relay.go`: High-performance WebSocket relay using `github.com/coder/websocket`.
    - `/agent`: Authenticated with `AuthAgent`, bidirectional binary streaming and text control frame handling.
    - `/view`: Authenticated with `AuthViewer`, pairs by `stream_id`, enforces maximum 2 viewers per stream.
    - Bounded channels per viewer (capacity 8): on queue overflow, drops oldest message and issues `KEYFRAME_REQ` (msg type 4) to the agent.
    - Sets 16 MB websocket read limit to support high-resolution full frames.
    - Zero frame storage, zero goroutine leaks on disconnect.
  - `gateway/main.go`: Gateway entrypoint listening on `:8082` with dev/production flag options.
  - `gateway/auth.go`: Production implementation of `Authenticator` using `golang-jwt/jwt/v5`.
    - Validates `iss`, `aud`, `exp`, `nbf`, `student_id`.
    - Enforces strictly pinned `HS256` signing algorithm (rejects `none` and mismatches).
    - Single-use `jti` replay prevention with thread-safe TTL map and expiration cleanup.
    - Agent identification via SHA-256 client certificate fingerprint from mTLS (`r.TLS.PeerCertificates[0]`).
- **Browser Viewer (`viewer/`):**
  - `viewer/tilecanvas.js`: HTML5 Canvas manager for batch tile rendering on `requestAnimationFrame`, automatic resizing, and privacy/secure desktop banner overlays.
  - `viewer/viewer.js`: Safe binary protocol parser using little-endian `DataView`, `ACK` and `KEYFRAME_REQ` encoders, and `ScreenViewer` managing WebSocket streaming, delta continuity checking, and `createImageBitmap` tile decoding.
  - `viewer/viewer.test.js`: Node.js test suite testing parser against WP1 golden fixtures (`keyframe.bin`, `delta.bin`, `full.bin`), fuzz testing, and protocol builders.

### Tests & Verification
- `gateway/relay_test.go` and `gateway/auth_test.go`:
  - `TestPairingAndForwardingBothDirections`: Agent-to-viewer and viewer-to-agent bidirectional flow.
  - `TestThirdViewerRefused`: Rejection of 3rd viewer with HTTP 429.
  - `TestQueueOverflowTriggersKeyframeReq`: Fast sender overflowing viewer channel causes drop-oldest and triggers `KEYFRAME_REQ`.
  - `TestAuthRejections`: HTTP 401 when agent or viewer fail authentication.
  - `TestDisconnectCleanup`: Clean disconnection without goroutine or channel leaks.
  - `TestAuthViewerValidationAndReplayPrevention`: Replayed `jti` rejected, expired JWT rejected, wrong audience/issuer rejected, `none` algorithm rejected.
  - `TestAuthAgentMTLSFingerprint`: Accurate SHA-256 fingerprint extraction from TLS peer certificate.
  - All Go tests passing 100% under `go test -race -v ./...`.
- `viewer/viewer.test.js`:
  - All 5 test cases passing (100%) in Node.js test runner against generated fixtures.


## WP3 & Windows Capture: Agent State Machine and Capture Backends (T8, T9, T10) - COMPLETE & VERIFIED

### What exists now
- **Agent State Machine (`agent/`):**
  - `agent/agent_state.h` & `agent/agent_state.cpp`: `AgentStateMachine` implementing states:
    - `IDLE`, `ARMED`, `STREAMING`, `PAUSED_MASKED`, `PAUSED_SECURE`, `PAUSED_DISCONNECTED`.
    - Threaded capture loop with `FlowControl` pacing, dynamically encoding changed tiles.
    - Zero capture threads and ~0% CPU usage in `IDLE`.
    - Handles incoming `view_start{mode, stream_id}` (live and snapshot modes) and `view_stop`.
    - Processes incoming binary `Ack` and `KeyframeReq` messages.
    - Clean shutdown and thread join semantics with recursive mutex protection against deadlocks.
- **Capture Implementations (`capture/`):**
  - `capture/win_gdi_capture.h` & `capture/win_gdi_capture.cpp`: Win32 GDI `BitBlt` capture implementing `ScreenCapture` using `CreateCompatibleDC` and `CreateDIBSection` with full dirty flagging.
  - `capture/win_dxgi_capture.h` & `capture/win_dxgi_capture.cpp`: DXGI Desktop Duplication on owning adapter with `EnumAdapters1`, `DuplicateOutput`, dirty rect extraction via `GetFrameDirtyRects`, staging texture mapping, and automatic `ACCESS_LOST` / `ACCESS_DENIED` status reporting.
  - `capture/x11_capture.cpp`: Linux X11 capture using `XShmGetImage`, `XDamage` dirty rectangle tracking, and `XRandR` display resolution change detection.
- **Tests (`tests/test_agent_state.cpp`):**
  - 10-frame streaming capture verification through `AgentStateMachine`.
  - Capture thread lifecycle and clean termination on `view_stop`.
  - Snapshot mode: captures exactly one frame then transitions back to `ARMED` with thread joined.
  - Privacy masking state transitions (`PAUSED_MASKED`).
  - Secure desktop detection (`PAUSED_SECURE`).
  - Network disconnection and recovery (`PAUSED_DISCONNECTED` -> `ARMED`).

### Status
- Build: GCC 14.2.0 (MinGW) / C++17 clean (0 warnings).
- Test suite: 25 test cases, 1054 assertions passing (100% green).
- `ctest` runs 100% green.


## WP4: Privacy, Modes, Capabilities (T12, T15, T16) - COMPLETE & VERIFIED

### What exists now
- **Privacy & Audit (`agent/privacy.h`, `agent/privacy.cpp`):**
  - `PrivacyFilter`: Configurable rules with `process_name` (case-insensitive), `window_class`, and `title_regex`.
  - Fail-safe masking: Any rule match or provider exception triggers `MASKED` placeholder frame.
  - Exam profile override: Disables privacy masking, allowing unhindered exam monitoring.
  - Audit logging: Tracks mask start and end transitions with duration in milliseconds.
- **Downscaling & Wall Mode (`core/downscale.h`, `core/downscale.cpp`):**
  - `downscale_box`: Area box filter downscaling raw BGRA frames preserving aspect ratio.
  - Computes weighted area averaging over 2D pixel blocks.
  - Integrated into `AgentStateMachine` for `wall` mode (320px wide thumbnails, 5-second pacing, full keyframes only).
- **Capabilities Reporting (`agent/capabilities.h`, `agent/capabilities.cpp`):**
  - `query_capabilities`: Identifies OS and session type.
  - Wayland detection (`XDG_SESSION_TYPE=wayland`): Sets `silent_capture=false`, `live=false`, `snapshot=false`, `delta=false`, `multi_monitor=false`.
  - JSON serialization (`to_json`): Formats capabilities into compact JSON string for CCC reporting.
- **Indicator (`agent/indicator.h`, `agent/indicator.cpp`):**
  - Persistent indicator management for active screen viewing states.
- **Agent Modes & Cursor:**
  - `snapshot`: Encodes single frame at quality 95 and returns to `ARMED`.
  - `live`: Flow-controlled adaptive delta streaming (1–5 FPS).
  - `wall`: Downscales to 320px thumbnails every 5s, never deltas.
  - `send_cursor`: Separate `MsgType::Cursor` wire protocol messages for pointer positions and shapes.
- **Tests (`tests/test_downscale.cpp`, `tests/test_capabilities.cpp`, `tests/test_privacy.cpp`):**
  - `Downscale: Preserves aspect ratio`: Aspect ratio calculation for 1080p, 768p, and error conditions.
  - `Downscale: Averages correctly on checkerboard`: Multi-channel color averaging across 4x4, 2x2, and 1x1 grids.
  - `Capabilities: Wayland session sets capture to false`: Environment mocking disables capture on Wayland.
  - `PrivacyFilter: Rule matching by process, class, and title regex`: Multi-criteria matching.
  - `PrivacyFilter: Provider failure or throw triggers safe MASKED`: Fail-safe protection on OS errors.
  - `PrivacyFilter: Exam mode overrides all masking rules and errors`: Exam policy enforcement.
  - `PrivacyFilter: Audit logging records mask transitions and duration`: Millisecond timing precision.

### Status
- Build: GCC 14.2.0 (MinGW) / C++17 clean (0 warnings).
- Test suite: 34 test cases, 1925 assertions passing (100% green).
- `ctest` runs 100% green.


## WP5: Laravel CCC API, Screen Health Classifier & Web Viewer UI (T13, T17) - COMPLETE & VERIFIED

### What exists now
- **API & Token Issuance (`server/app/Http/Controllers/Api/ViewTokenController.php`):**
  - `POST /api/screen/{student}/token`:
    - Strict session ownership verification: Only the instructor who started the active lab session (or an admin) can view the student screen; requests from instructors of other labs/sessions are rejected with HTTP 403 Forbidden.
    - Generates 30-second single-use JWT with HS256 algorithm and required claims (`iss`, `aud`, `sub`, `student_id`, `lab_session_id`, `stream_id`, `mode`, `iat`, `nbf`, `exp`, `jti`).
    - Records start in `screen_view_audits` database table.
  - `POST /api/screen/{audit}/renew`:
    - Renews active stream lease for additional 30 seconds with fresh single-use `jti` while session remains active.
  - `POST /api/screen/{audit}/stop`:
    - Terminates screen view stream and records `ended_at` timestamp.
  - Revocation hook: Instructor logout automatically terminates all active screen view audits.
- **Database (`server/database/migrations/2026_11_11_000200_create_screen_view_audits_table.php`, `server/app/Models/ScreenViewAudit.php`):**
  - Migration and model for `screen_view_audits`: `instructor_id`, `student_id`, `lab_session_id`, `stream_id`, `mode`, `started_at`, `ended_at`.
- **Health Classifier (`server/app/Services/AgentHealthClassifier.php`):**
  - Pure function `classify($pc, $lab_snapshot, $now)`:
    - Missed 3 heartbeats (>30s) -> `AGENT_UNAVAILABLE`.
    - Escalates to `SUSPECTED_TAMPERING` only if ALL four conditions hold:
      1. Unavailable longer than 5 minutes (>300s).
      2. At least 80% of other PCs in same lab are still connected.
      3. No sleep or shutdown event reported by PC.
      4. No reconnect has occurred.
    - Whole-lab outage maintains `AGENT_UNAVAILABLE`.
- **Web Viewer UI & Alpine Integration (`server/resources/views/dashboard/live.blade.php`, `server/resources/js/app.js`):**
  - Drawer tab: Added `screen` tab to PC detail drawer.
  - Capabilities badge: Displays platform badges (`Snapshot`, `Live`, `Delta`, `Multi-Monitor`, `Secure Desktop`, or `Wayland (Unsupported)` warning).
  - Controls: `Snapshot` button and `Live Stream` / `Stop Live` toggle with lease auto-renewal timer.
  - HTML5 Canvas screen view with `ScreenViewer` integration.
- **Tests (`server/tests/Unit/AgentHealthClassifierTest.php`, `server/tests/Feature/ViewTokenTest.php`):**
  - 8/8 unit tests for health classifier rules.
  - 6/6 feature tests for token issuance, 403 rejection of other instructors, JWT claim verification, audit creation, lease renewal, and stop.

### Status
- Laravel test suite: 41 test cases, 234 assertions passing (100% green).
- Vite asset compilation: Clean build with 0 errors (`app.js` & `app.css`).



