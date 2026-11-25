# Agent ↔ CCC protocol (v1)

All agent traffic is **outbound** from the PC to the CCC on TCP 8443 (nginx, TLS 1.3). No port is
opened on student PCs. Source of truth for field names: `agent/core/include/xg/protocol.hpp`,
`agent/core/include/xg/event.hpp`, `server/app/Services/EventIngestor.php`.

## Discovery (UDP 47474, optional)
Agent broadcasts `{"probe":"XEPHYGUARD_DISCOVER","v":1}`; the CCC answers
`{"service":"xephyguard-ccc","host":"192.168.10.2","api_port":8443,"ca_fingerprint":"<sha256>","lab":"Lab 3"}`.
The reply is unauthenticated, so the agent uses it only for `host`/`api_port`. It never trusts the reply's
`ca_fingerprint`: the CA is pinned from `--ca-fingerprint` (installers, Add-a-PC page) or, only when the admin
passes `--trust-on-first-use`, from the first download. Every later TLS connection is verified against that CA.

## Enrollment ("approve, don't configure")
| Step | Request | Auth |
|---|---|---|
| 1 | `GET http://<ccc>/ca.crt` (only if the CA is not installed yet; fingerprint must match) | none |
| 2 | `POST /api/agent/v1/enroll` `{enroll_key \| enroll_token, hostname, os, os_version, macs[], machine_uid, agent_version, csr_pem}` → `201 {enrollment_id, poll_token, status:"pending"\|"approved"}` | lab key (rotatable) → stays `pending` until staff approve; or a one-time per-PC `enroll_token` (`xgen_` + 40 chars, made on `/enroll`, expires, stored hashed) → `approved` at once and the token is used up; a given token that is unknown, used, revoked or expired → 403. Rate-limited |
| 3 | `GET /api/agent/v1/enroll/{id}?poll_token=…` → `pending` \| `approved {pc_id, name, token, cert_pem, ca_pem}` \| `rejected` \| `collected` | poll token |

The CSR key is EC P-256, generated on the PC (private key 0600, never leaves the PC). The CCC signs a
client certificate (`extendedKeyUsage=clientAuth`, 825 days) and issues a 64-character bearer token
(stored hashed, bound to the cert fingerprint). Token and certificate are handed out **once**. `machine_uid` (32 hex) is required. Re-enrolling with a known `machine_uid`
keeps the PC's history and revokes the old credentials. A PC that only shares the hostname or a MAC is
flagged on the request (`conflict_pc_id`) and never merged; staff may adopt it explicitly if it has no uid.

## Authenticated requests
Every request after enrollment carries the client certificate (verified by nginx against the lab CA)
**and** `Authorization: Bearer <token>`. Laravel checks that the certificate fingerprint equals the
one issued with that token (`AuthenticateAgent`). Per-agent rate limit: 120 requests/min.

### `GET /api/agent/v1/policy`
```json
{ "policy": { "version": 509072055, "mode": "test", "session_id": 12, "profile": "Test: AI blocked",
              "title_capture": "app_only", "collect": {"window": true, "...": true},
              "intervals": {"heartbeat_s": 5, "...": 0}, "actions": {"default": {}, "rules": {"proc.ollama": ["log","warn","kill","alert"]}},
              "disabled_rules": [], "work_dirs": ["~/lab"], "signatures_version": "2026.11.01-…", "notice_text": "…" },
  "signatures": { "version": "…", "bundle": "<canonical JSON text>", "signature": "<base64 ECDSA-SHA256 by the CA key>" },
  "realtime": { "app_key": "…", "port": 8443, "channel": "private-pc.12", "auth_path": "/api/agent/v1/broadcasting/auth" } }
```
`policy.version` is a deterministic CRC of the policy. Agents refuse signature bundles whose signature
does not verify against the lab CA and report a `tamper/signature_invalid` event.

`policy.screen.privacy_rules` (optional, at most 128 entries) lists windows the live screen view must never show:
`[{"process": "keepassxc", "title_regex": "bank|incognito"}]`. A window matches an entry when every field that is set
matches (`process` = executable name, case-insensitive; `title_regex` = case-insensitive regex search over the first
512 bytes of the title); an entry needs at least one field, strings are at most 256 bytes. A malformed list rejects
the whole policy. The service passes the list to the helper in `config` as `screen.privacy_rules`; the helper keeps
the focused window in memory only to decide the mask and never sends it for this purpose. While the focused window is
unknown, a rule does not compile, or the list is malformed, the helper masks the screen (a `MASKED` placeholder
frame, no pixels). The server reads the list from `data/screen/privacy_rules.yaml` (`rules:` entries with `process`/`title_regex`),
drops entries that would make the agent reject the policy (bad type, over 256 bytes, no field, invalid regex), keeps at most
128, and omits `privacy_rules` when the list is empty or unreadable (and `screen` when nothing else is in it).

`policy.screen.codec` (`"jpeg"` | `"webp"`, default `jpeg`), `policy.screen.quality` (integer 10..100, default 85) and
`policy.screen.webp_method` (integer 0..6, default 1; WebP only) set how screen tiles are encoded (cost only, not what is
shown; measured in docs/EVALUATION.md M4-5). All optional; a missing or invalid value keeps the default and never rejects
the policy. The service passes them to the helper in `config.screen`; a view in progress keeps its settings. The server
does not send them yet (agent defaults apply).
`policy.screen.cpu_budget_pct` (number 1..100, default 8, optional, invalid keeps the default) is the CPU budget of the
helper's capture/encode thread in percent of one core; the governor lowers fps (floor 2) then quality to stay under it.
Passed in `config.screen` like the above, applied at once (also to a running view). The server sends it only after an
admin set it on the Settings page (`Setting` `screen.cpu_budget_pct`, integer 1..100); otherwise the agent default applies.

`policy.network_lock` (`off` | `observe` | `strict`, default `off`, DECISIONS #23) is applied only when
`proxy_mode` is `proxy_only` and the mode is not `always_on`. `observe` drops QUIC (UDP 443) to public
addresses; `strict` drops every public destination except the CCC. If the local rules disappear the agent
re-applies them and reports `tamper/network_lock_removed`.

### `POST /api/agent/v1/events`
```json
{ "v": 1, "pc_id": "12",
  "heartbeat": { "agent_version": "0.1.0", "mode": "test", "user": "alice", "local_time_ms": 1793500000000,
                 "policy_version": 509072055, "signatures_version": "…", "queue_depth": 0, "helpers": 1,
                 "capabilities": {"keys": true, "...": true}, "usage": {"cpu_pct": 0.4, "rss_kb": 14744, "bytes_up": 17385} },
  "events": [ { "seq": 1041, "ts": 1793500000123, "type": "rule_hit", "data": { "...": "..." } } ] }
```
Response: `{ack_seq, server_time_ms, policy_version, signatures_version, isolated}`.

* `usage.screen` (only in a heartbeat after screen frames went up): `{"fps": 6.5, "kbps": 820, "cpu_pct": 5.9, "level": 3}`.
  `fps`/`kbps` are frames and kbit/s the service relayed since the previous heartbeat; `cpu_pct` (capture thread, % of
  one core) and `level` (governor throttle, 0 = none) come from the helper's `screen_cost` and are left out when
  older than 30 s. Numbers only, nothing from the screen (screen Step 57). The server keeps only these four numbers
  and shows them in the PC drawer.
* Max 500 events / 1 MB per batch, 64 kB per event; malformed batches are rejected with 422.
* `seq` is monotonic per installation and survives restarts and acknowledgements (SQLite queue).
  The CCC ignores `seq <= agents.last_seq` and has a unique `(agent_id, seq)` index, so a batch
  re-sent after a lost acknowledgement is harmless.
* Clock: the CCC corrects event times by `server_now - local_time_ms` (the CCC is the time authority)
  and the agent computes its own offset from `server_time_ms` (reported in heartbeats).
* A changed `policy_version`/`signatures_version` in the ack makes the agent re-fetch the policy.
* `heartbeat.core_dropped` (diagnostic counter, no personal data): internal messages the agent core dropped
  because its bounded mailbox was full (DECISIONS #22). Non-zero means some local observations may be missing.
* `heartbeat.network_lock`: the session network lock that is really installed on the PC (`off` / `observe` /
  `strict`). It differs from the session setting while the rules cannot be applied.

### Event types
The table is the server's allow-list (`server/app/Services/Ingest/EventSchema.php`, DECISIONS #27): every other field,
and a listed field with the wrong type, is dropped before storage; strings are cut to the length in brackets
(default 64). Every event may also carry `user` [64]. Change both together.

| type | data (numbers and names only — never typed text, clipboard text or screen content) |
|---|---|
| `heartbeat` | carried in the batch envelope (see above), every 5 s (15 s in Always-on). Allowed: `agent_version` [32], `uptime_s`, `agent_uptime_s`, `mode` [16], `session_id`, `policy_version` (int or string [20]), `signatures_version` [64], `user` [64], `sessions[]` (≤ 32 × [96]), `clock_offset_ms`, `local_time_ms`, `queue_depth`, `helpers`, `realtime`, `suppressed_hits`, `core_dropped`, `network_lock` [8], `capabilities` (≤ 64 scalar entries or one-level maps, keys ≤ 32), `usage` {`rss_kb`, `cpu_pct`, `bytes_up`, `screen`} |
| `agent_started` | `version` [32], `previous_exit` (clean\|unclean) [16], `os` [128], `hostname` [255] |
| `agent_stopping` | `reason` [32], `uptime_s` (clean shutdown announcement) |
| `process_start` / `process_stop` | `pid, ppid, exe` [256], `rss_mb, rule_id` (stop: `pid, rule_id`) — only for processes matching signatures; no command lines |
| `process_snapshot` | `hash, count, total_running, added[], removed[]` (≤ 200 executable names each), `list[]` (≤ 250 × `pid, ppid, exe, name, user` [32], `rss_mb, cpu_pct, net_kbps, net_conns, start, rule_id`; `start` = OS start tick that identifies the process together with `pid`) |
| `rule_hit` | `rule_id, severity` [16], `category` [32], `description` [512], `subject` [512], `actions[]` (≤ 8), `result` {`kill` {`pid, killed, reason`}}, `context`, `mode` [16], `session_id, user`. `context` keys: `exe, pid, user, port, adapter, driver, initial, default_routes, class, vendor, model, process, domain, remote_ip, remote_port, sent_kb, recv_kb, signal, ide, extension, version, activity, path` [512], `size_mb, rule_id, app, title` [256], `ai_site`. Tunnel flags (`category: tunnel`, rule id from `web.tunnel`, sessions only): context `process, pid, remote_ip, remote_port, signal: tcp_destination`, never closed by the agent |
| `listening_port` | `port, address, process` [128], `pid, rule_id, probed` |
| `window_focus` | `app` [64], `title` [256] (filtered by policy: off / app_only / full; the server drops a title the PC's effective `title_capture` does not allow, also `rule_hit.context.title`, and below `full` the " (file)" part of paste/copy targets), `ai_site` [128] |
| `paste` / `copy` | `chars, lines, code, score, src` [128], `dst` [128], `user` |
| `key_stats` | per 5 s window: `window_start, window_s, keys, backspaces, backspace_ratio, iki_mean/median/p90/std, bursts, max_pause, paste_chords, source` [16] |
| `net_adapter` | `change` [16], `name` [128], `driver, description` [128], `mac` [32], `addresses[]` (≤ 32), `up, wireless, virtual, default_route, kind, initial` |
| `device` | `action` [16], `subsystem` [32], `class` [32], `vendor` [16], `product` [16], `model` [128], `serial_hash, rule_id` |
| `extension_scan` | `count, hash, ai[]` (≤ 300 × `ide` [32], `id` [256], `version, user, rule_id, description` [512], `active, activity`), `added[], removed[]` (≤ 300 × [256]), `initial` |
| `artifact_scan` | `count, files_visited, truncated, found[]` (≤ 500 × `path` [512], `size_mb, rule_id, description` [512]) |
| `file_activity` | per work folder / 10 s: `dir` [256], `saves, created, deleted, bytes_delta, total_bytes, exts[]` (≤ 10 × [16]), `first_ts` |
| `gpu_sample` | `util_pct, mem_used_mb, mem_total_mb, gpu` [128] (only spikes) |
| `tamper` | `kind` [32] ∈ binary_hash_mismatch, binary_changed, helper_binary_missing, config_changed, ca_removed, ca_changed, clock_change, helper_missing, helper_terminated, signature_invalid, isolation_rule_removed, network_lock_removed; optional `detail` [256], `path` [512], `expected, actual` [128], `pinned, delta_ms, at, bundle_version` |
| `helper_status` | `connected, pid, session, display, caps` (map), `helper_version` |
| `command_result` | `command_id, status` [16], `detail` (map ≤ 32 entries) |
| `capabilities` | reserved (not sent by the current agent): `caps` (map) |

### Actions (policy per mode)
`log` (always) → `warn` (notice on the PC) → `kill` (opt-in per rule) → `alert` (CCC raises an alert) →
`isolate` (CCC moves the PC to the isolated nftables set). Default Test mode: log + warn + alert.

## Commands (CCC → agent)
Reverb (Pusher protocol 7) over `wss://<ccc>:8443/app/<key>`, mTLS required. The agent subscribes to
`private-pc.<id>`; the subscription is signed by `POST /api/agent/v1/broadcasting/auth`
(`{socket_id, channel_name}` → `{auth: "<key>:<hmac-sha256(secret, socket_id:channel)>"}`), which only
signs the agent's own channel. Event name `command`, data `{id, type, args}`:

| type | args | effect |
|---|---|---|
| `policy_updated` | – | re-fetch policy/signatures now (mode switch, override, publish) |
| `message` | `text, title?, urgent?` | notification in the user session |
| `kill` | `pid` (+ optional `start_time` from `process_snapshot.list[].start`) or `exe` [128] | terminate (exempt system processes are never killed); with `start_time` only that exact process, never a reused pid. The CCC sends a `pid` only if it is ≥ 2 and in the PC's latest `process_snapshot` (with the same `start` when `start_time` is given) |
| `isolate_notice` | `isolated` | tell the student network access was paused/restored |
| `rescan` | `what` ∈ processes, network, ports, devices, extensions, artifacts, all (the dashboard sends `processes` when none is chosen; the agent treats a missing `what` as `all`) | immediate scan: `processes` = process scan + `process_snapshot`; `network`/`ports` = port, adapter and AI-traffic scan (the known-port cache is cleared); `devices` = USB/device scan (`network` also includes it); `extensions` = extension + artifact scan; `artifacts` = local-model artifact scan; `all` = everything. The agent also accepts the aliases `installed` (= `extensions`) and `browser` (= `ports`); other values do nothing and still get `ok` |
| `ping`, `flush` | – | flush the queue now |
| `screen_lease` | `ttl_s` (1..3600, the agent's cap; the CCC sends the viewer lease, default 30) | the CCC has a live-view lease on this PC for `ttl_s` more seconds; sent with every screen token and every renewal. The agent captures the screen only while a lease is active (screen Step 15) |

Every command is acknowledged with a `command_result` event.
The CCC checks the arguments of every type before storing or sending a command (`CommandBus`, S-30): arguments not
listed above are dropped, invalid ones are refused with 422 (`message`: `text` ≤ 500, `title` ≤ 120; `rescan`: one of
the listed targets; `isolate_notice`: boolean).

## Screen relay (agent → CCC)
`wss://<ccc>:8443/screen-gw/agent`, mTLS required (403 without a verified client cert). Opened by the
root service only (DECISIONS #24); frames are the screen protocol in `xephyguard-screen/core/protocol.h`.
nginx drops any client-sent `X-Agent-Cert-Fingerprint` / `X-Agent-Token` and passes `X-SSL-Client-Verify`
and `X-SSL-Client-Cert` (URL-escaped PEM) to the gateway on 127.0.0.1:8082, which takes the agent identity
as the SHA-256 of that certificate (same value as `agents.cert_fingerprint`); it accepts no other agent
credential (no shared token, `X-Agent-Token` / `agent_token` are ignored). The policy response has no `screen`
object (no gateway address or token; the agent uses this fixed URL). The dashboard port 443
never forwards these headers and serves only `/screen-gw/view` and `/screen-gw/health` (agent path 404). The gateway binds the connection to the PC of that certificate (internal lookup below):
`?stream_id` naming another PC is refused with 403, and frames whose header `stream_id` is not that PC are
dropped; an unenrolled or revoked certificate gets 401. The service opens it (`?stream_id=<pc_id>`, numeric only) while a helper of the
active session streams over IPC (`screen_caps.ipc_stream`), relays helper frames up unchanged (queue capped at
8 MiB, excess dropped) and gateway messages down as `screen_rx`; reconnect backoff 1 s doubling to 30 s.
Messages from the gateway are capped at 64 KiB per WebSocket message (fragments included) and 1 KiB per relayed message.

Screen frame records (`xephyguard-screen/core/protocol.h`, little-endian; 28-byte header, then `tile_count` records):
- v1 (header byte 2 = 1): record = `x`,`y` (u16, tile indices on a 64 px grid), `codec` (u8), reserved (u8), `len` (u32), payload; `len` <= 16384.
- v2 (screen Step 26, header byte 2 = 2, `Frame` messages only): record = `x`,`y`,`w`,`h` (u16 pixels), `codec` (u8: 0 raw BGRA, 1 WebP, 2 JPEG), reserved (u8), `len` (u32), payload (14-byte record header).
  Decoders reject a record with `w` or `h` outside 1..256, a rectangle not inside the header `width` x `height`, a raw payload that is not exactly `w*h*4` bytes, `len` > 262144, or a v2 message of another type with records.
  The reference viewer (`xephyguard-screen/viewer`, Step 27) parses and draws both versions with the same bounds checks; the dashboard's copy in `server/public/js/app.js` still reads v1 only, so agents keep sending v1. The encoder (Step 28) emits v2 only when `EncoderConfig.rectangles` is set (off by default; `xgstreambench --rects`): dirty 64 px tiles are merged into rectangles of whole tiles, at most 4x4 tiles (256x256 px), clipped to the frame, one image per rectangle (a full 1920x1080 frame is 40 images, not 510). Turning it on for agents waits until the dashboard viewer reads v2. The gateway reads only the header and relays either version; viewer-to-agent `Ack`/`KeyframeReq` stay v1.

Multiple monitors (screen Step 35). A frame's header `monitor_id` is 0 for the whole desktop picture (the default; also the only value on a single monitor) or `n` (1..N) for monitor `n` alone, and `width`/`height` are that picture's size. The tile limit per message is 16384 (`kMaxTiles`; was 4096): a 7680x4320 picture is 8160 tiles. A viewer asks for a monitor with a `KeyframeReq` whose `monitor_id` is `n`, or `0xFFFF` for the whole desktop; `0` means "keep what is shown" (every other keyframe request, so a recovery never switches monitors). An unknown id is ignored. The agent then restarts the picture: the next frame is a full keyframe of the new size. One monitor is shown per PC at a time: the last request wins for all viewers, and the gateway lets at most 2 monitor switches per second past its keyframe rate limit.
Monitors message (agent → viewers, `type` 6, v1 only): the 28-byte header (`monitor_id` = the monitor being shown, 0 = whole desktop; `flags` bit `ALL_SCREENS` (128) = the agent can also show the whole desktop; `tile_count` = record count, at most 16), then one 8-byte record per monitor: `x`,`y`,`w`,`h` (u16 pixels inside the whole-desktop picture). Monitor ids are the 1-based record positions. Sent only when there are 2 or more monitors, before the picture, on every `KeyframeReq`. The gateway keeps the newest one per stream and replays it to a viewer that joins later; viewers do not ACK it. Geometry only, nothing from the screen. Linux lists XRandR monitors (primary first, then top to bottom, left to right; a monitor containing another is left out). Windows (screen Step 36, compile-checked only) lists every DXGI output attached to the desktop and shows one at a time: it starts on output 1, has no whole-desktop view (`ALL_SCREENS` clear, `0xFFFF` refused). The GDI fallback and the benchmark capture list none. The pointer position (Cursor message) is relative to the shown monitor and not sent while the pointer is on another one. The dashboard's copy in `server/public/js/app.js` shows the same picker (buttons "All screens" / "Monitor n" in the PC drawer) and reads up to 16384 tiles.

Cursor message (screen Step 34, `type` 2, flags `CURSOR_F`, no records, live mode only): header `width`/`height` carry the pointer position `x`/`y` in screen pixels. The helper sends it at most 30 times a second (every 33 ms while the pointer moves, every 100 ms when still for 1 s; only when the position changed), only while the view is streaming and not privacy-masked. Position only: never the cursor image. The viewer does not ACK it and draws a marker overlay over the canvas. The gateway relays it best effort (dropped, never queued over frames, when a viewer's queue is full).

## Screen view lease (dashboard → gateway)
`POST /api/screen/pc/{pc}/token`, `POST /api/screen/{student}/token` and `POST /api/screen/{audit}/renew` return an
HS256 JWT (30 s) with `iss`, `aud`, `sub` (user who opened the view, kept on renewal), `audit_id`, `pc_id`, `stream_id`, `student_id`, `lab_session_id`, `mode`,
`iat`, `nbf`, `exp`, `jti`. `stream_id` is always the PC's id (the stream the agent opens) and is set by the server;
a client-sent `stream_id` is ignored. A renewed token keeps the audit's `pc_id`; an audit without one is not renewed.
The gateway rejects (401) a token without `exp`, with a zero `stream_id` or `pc_id`, with `pc_id != stream_id`, or
with a `mode` other than `live`, `snapshot`, `wall` (no `student_id` fallback). A `snapshot` viewer gets one keyframe,
then the gateway closes the socket (1000 "snapshot delivered").
The token is never in the URL (URLs reach proxy logs and browser history). A browser connects to `/view` with no credential;
the gateway accepts the upgrade and waits 5 s for the first message, the text `{"type":"auth","token":"<jwt>"}` (≤ 4 KiB), and
applies the checks above to it (the token is single use). A missing, late, binary, malformed or invalid first message closes the
socket (1008), and a token in the query string is ignored. Non-browser clients may still send `Authorization: Bearer <jwt>` at upgrade.
The viewer sends binary controls only after the auth message. If the PC already has the maximum of 2 viewers the socket closes with 1013.
The socket closes at the lease `exp` (4001 "lease expired") unless the viewer renews in band: it sends the text
message `{"type":"renew","token":"<jwt from /renew>"}` (≤ 4 KiB); the new token must be valid, unused and grant the same
`stream_id`, `pc_id` and `mode`; the gateway extends the lease and answers `{"type":"lease","expires_at":<unix>}`.
Any other text message, or a refused renewal, closes the socket (1008). Binary viewer messages reach the agent only
if they are exactly 28 bytes with the screen magic and version and type `Ack` (3) or `KeyframeReq` (4); the gateway
sets their `stream_id` to the viewer's stream and drops everything else. A viewer message over 4 KiB closes the socket (1009).
A browser `Origin` must be the request host (nginx forwards the dashboard `Host`) or match `-allowed-origins`
(`SCREEN_GATEWAY_ALLOWED_ORIGINS`, comma-separated `host[:port]` patterns), else the upgrade gets 403.
The gateway keeps the agent's last keyframe for at most 5 s and replays a copy to a new viewer with frame flag
`CACHED` (64, `core/protocol.h`; never sent by agents). Any frame flagged `MASKED` (8) or `SECURE` (16) clears the
cache, and such keyframes are never cached. The renewal must also keep `sub` and `audit_id`.
Catch-up (screen Step 25): a viewer gets no delta frame until it has a keyframe the deltas build on (a new viewer,
one whose queue overflowed, or every viewer after an agent reconnect); an overflow empties that viewer's queue only,
other viewers are not affected. `MASKED`/`SECURE` placeholders always go through. KeyframeReqs to the agent, from
viewers or from the gateway, are limited to 1 per second per stream; one held back is sent when the second ends.

Gateway → agent `Status` (type 5, 28-byte header, sent whenever the viewer set changes and before the `KeyframeReq`
that a new viewer triggers): `flags` and `width` = number of viewers (0 = nobody watches, stop capturing), and the
reserved header field at bytes 26-27 (u16 LE) = the view mode the agent must serve: `0` none/not stated, `1` live,
`2` snapshot, `3` wall. With several viewers the most demanding mode wins (live > snapshot > wall). A viewer count above
0 with mode `0` means live. Agents that do not read the field behave as before.

Wall mode (agent → viewer): one 320 px wide keyframe thumbnail per period (5 s), sent only when the downscaled
image changed. Otherwise the agent sends an "unchanged" Frame: the 28-byte header alone, `flags` 0, no tiles,
`seq` = `base_seq` = the last thumbnail's `seq`. Viewers draw nothing for it; the gateway forwards it and does not cache it.
A `KeyframeReq`, a lifted mask, a secure-desktop pause or a reconnect makes the next thumbnail a full one.

Pauses (agent → viewer): a `MASKED` (8) or `SECURE` (16) placeholder frame (header only) is sent once when the pause
begins and again for each `KeyframeReq` during it; nothing else is sent until it ends. `SECURE` covers the Windows
secure desktop (lock screen, UAC) and, on X11, a blanked display (DPMS standby/suspend/off; not checked more than 4 times/s).

Server → gateway revoke (CCC-internal, loopback only, never proxied by nginx): `POST http://127.0.0.1:8082/internal/revoke`
with `Authorization: Bearer $XG_INTERNAL_TOKEN` and JSON `{"audit_id":N}`, `{"user_id":N}` and/or `{"lab_session_id":N}`
(any non-zero field matches the lease's `audit_id`, `sub`, `lab_session_id`). Matching viewers are closed (4003 "lease
revoked") and tokens issued at or before the revoke are refused on connect and renewal for 1 h. Answer `{"closed":N}`.
The server calls it (best effort, 2 s timeout, `SCREEN_GATEWAY_INTERNAL_URL`, default `http://127.0.0.1:8082`) on view stop
(`audit_id`), logout and on deactivating or demoting a user below instructor (`user_id`), and on session end (`lab_session_id`).

Gateway → server (CCC-internal, loopback): `GET /api/internal/screen/agent/{sha256 fingerprint}` with
`Authorization: Bearer $XG_INTERNAL_TOKEN` returns `{"pc_id": N}` for an enrolled, unrevoked agent on a
non-retired PC, else 404. The gateway uses it to bind an agent connection to stream `pc_id`.

## Helper ↔ service IPC
JSON lines over `/run/xephyguard/helper.sock` (Linux, peer checked with `SO_PEERCRED` + `/proc/<pid>/exe`)
or `\\.\pipe\XephyGuardHelper` (Windows, client image path + session id checked). See
`agent/core/include/xg/ipc_messages.hpp`.

Helper → service also carries binary **screen frame** records (`agent/core/include/xg/ipc_framing.hpp`,
DECISIONS #24): `0x00`, type `0x01`, payload length (4 bytes, big-endian, 1 .. 4 MiB), payload = one encoded
screen-protocol message. A JSON line never starts with `0x00`. The service checks type and length only, never
decodes the payload, and closes the connection on an unknown type, an empty or an oversize record. Records are
accepted only on connections that passed the helper peer check above, and relayed only from the helper the
service chose as source: one that sent `{"t":"screen_caps",...,"ipc_stream":true}` from the active session.
Service → helper: `{"t":"screen_rx","data":"<base64 gateway message>"}`, and `{"t":"screen_lease","ttl_s":N}`
(1..120) while the CCC holds a live-view lease (CCC command `screen_lease`, granted with every screen token and renewal;
the service sends it to the source helper on each grant and when a helper becomes the source). Without a lease the
service drops helper frames and sends no `screen_rx`; it keeps the newest gateway Status/KeyframeReq seen for 10 s
and replays it once when a lease arrives (the viewer's request can beat the command). The helper starts capturing
only inside a lease, stops by itself when `ttl_s` lapses or the service link drops, and a gateway request alone never
starts it (screen Step 15).
Helper → service every 10 s while viewed: `{"t":"screen_cost","cpu_pct":5.9,"level":3}`; accepted only from the
source helper, numbers clamped (cpu_pct 0..1000, level 0..1000), anything else ignored.
`screen_rx` data is one gateway message. The helper's `config` carries only
`screen.enabled`, `screen.privacy_rules` (mask rules, see the policy section) and `screen.codec`/`quality`/`webp_method`/`cpu_budget_pct`: no gateway address, port or token reaches the student session (screen Step 61); the helper opens
no network connection for the screen.

## Proxy → CCC (internal)
`POST http://127.0.0.1:8000/api/internal/net-events` with `Authorization: Bearer $XG_INTERNAL_TOKEN`,
`{events:[{client_ip, host, path?, layer: sni|http|path|search-ai, action: blocked|allowed|rewritten|observed, category?, matched?, ts}]}`.
Limits: ≤ 1000 events, `host` ≤ 255, `path` ≤ 512, `layer` ≤ 16, `category` ≤ 24, `matched` ≤ 255 chars, `ts` in ms.
`observed` = a passed (not blocked) connection or request to a known AI (`category: ai`) or IDE-AI backend
(`category: ide`) domain or path; the proxy sends it at most once per (client IP, matched domain) per 30 s. For a known
PC the CCC raises a `proxy.ai_observed` / `proxy.ide_ai_observed` alert (high, at most one per PC, session and title per
30 min); other categories and unknown IPs are only stored in `net_events`.
