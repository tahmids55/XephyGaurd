# F47. Standalone screen agent (xgagent)

- **Component:** screen
- **Area:** 8. Screen viewing and evidence
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** retired (DECISIONS #25). It was a separate screen-only agent from the WP0–WP6 work.
- **Code:** program and CMake target removed (docs/plan/screen.md Step 43). `xephyguard-screen/capture/`
  and `core/` stay as the shared capture/encode library used by the helper-embedded streamer and `xgbench`.
- **Status:** retired. Screen streaming is only through the helper-embedded streamer
  ([F43](F043-live-screen-view-of-a-pc.md)).
- **Test:** none; nothing to test.
