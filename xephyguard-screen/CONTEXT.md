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
