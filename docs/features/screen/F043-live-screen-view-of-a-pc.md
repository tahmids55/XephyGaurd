# F43. Live screen view of a PC ⚠PRIV ⚠SEC

- **Component:** screen (also: server, agent)
- **Area:** 8. Screen viewing and evidence
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff watch a PC's screen live (single view, screen wall, seat-to-seat switching).
- **Trigger:** dashboard → `POST /api/screen/pc/{pc}/token` or `POST /api/screen/{student}/token`
  → browser opens `ws(s)://<ccc>:8082/view` (or `/screen-gw/view` via nginx) and sends the token as its first message.
- **Code:** server `S/Http/Controllers/Api/ViewTokenController.php`; gateway
  `xephyguard-screen/gateway/relay.go`, `auth.go`; viewer `xephyguard-screen/viewer/`;
  agent side `A/apps/screen_streamer.cpp` (inside the helper), `xephyguard-screen/capture/`.
- **In/out/side effects:** writes `screen_view_audits`; 30 s JWT leases; gateway keeps the last
  keyframe in RAM.
- **Status:** partly working (`owning_instructor_gets_valid_jwt_and_audit`, `lease_renewal_and_stream_stop`,
  `issue_token_directly_for_pc_without_session`). Problems: plaintext `ws://` by default, any user
  can view an idle PC and pick another PC's `stream_id`, privacy mask never turned on, conflicts
  with ETHICS.md (`docs/HISTORY.md`).
- **Test:** 1. Start the gateway and a helper on a PC. 2. Click the seat's Screen tab. 3. The desktop shows.
