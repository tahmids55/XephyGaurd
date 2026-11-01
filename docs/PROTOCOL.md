# Agent ↔ CCC protocol (v1)

All agent traffic is **outbound** from the PC to the CCC on TCP 8443 (nginx, TLS 1.3). No port is
opened on student PCs. Source of truth for field names: `agent/core/include/xg/protocol.hpp`,
`agent/core/include/xg/event.hpp`, `server/app/Services/EventIngestor.php`.

## Discovery (UDP 47474, optional)
Agent broadcasts `{"probe":"XEPHYGUARD_DISCOVER","v":1}`; the CCC answers
`{"service":"xephyguard-ccc","host":"192.168.10.2","api_port":8443,"ca_fingerprint":"<sha256>","lab":"Lab 3"}`.
The reply is unauthenticated: the agent pins the CA fingerprint (from `--ca-fingerprint` or the reply)
and verifies every later TLS connection against that CA.

## Enrollment ("approve, don't configure")
| Step | Request | Auth |
|---|---|---|
| 1 | `GET http://<ccc>/ca.crt` (only if the CA is not installed yet; fingerprint must match) | none |
| 2 | `POST /api/agent/v1/enroll` `{enroll_key, hostname, os, os_version, macs[], agent_version, csr_pem}` → `201 {enrollment_id, poll_token, status:"pending"}` | lab key (rotatable), rate-limited |
| 3 | `GET /api/agent/v1/enroll/{id}?poll_token=…` → `pending` \| `approved {pc_id, name, token, cert_pem, ca_pem}` \| `rejected` \| `collected` | poll token |

The CSR key is EC P-256, generated on the PC (private key 0600, never leaves the PC). The CCC signs a
client certificate (`extendedKeyUsage=clientAuth`, 825 days) and issues a 64-character bearer token
(stored hashed). Token and certificate are handed out **once**. Re-enrolling a PC with a known MAC
keeps its history and revokes the old credentials.

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

### `POST /api/agent/v1/events`
```json
{ "v": 1, "pc_id": "12",
  "heartbeat": { "agent_version": "0.1.0", "mode": "test", "user": "alice", "local_time_ms": 1793500000000,
                 "policy_version": 509072055, "signatures_version": "…", "queue_depth": 0, "helpers": 1,
                 "capabilities": {"keys": true, "...": true}, "usage": {"cpu_pct": 0.4, "rss_kb": 14744, "bytes_up": 17385} },
  "events": [ { "seq": 1041, "ts": 1793500000123, "type": "rule_hit", "data": { "...": "..." } } ] }
```
Response: `{ack_seq, server_time_ms, policy_version, signatures_version, isolated}`.

* Max 500 events / 1 MB per batch, 64 kB per event; malformed batches are rejected with 422.
* `seq` is monotonic per installation and survives restarts and acknowledgements (SQLite queue).
  The CCC ignores `seq <= agents.last_seq` and has a unique `(agent_id, seq)` index, so a batch
  re-sent after a lost acknowledgement is harmless.
* Clock: the CCC corrects event times by `server_now - local_time_ms` (the CCC is the time authority)
  and the agent computes its own offset from `server_time_ms` (reported in heartbeats).
* A changed `policy_version`/`signatures_version` in the ack makes the agent re-fetch the policy.

### Event types
| type | data (numbers and names only — never typed text, clipboard text or screen content) |
|---|---|
| `heartbeat` | carried in the batch envelope (see above), every 5 s (15 s in Always-on) |
| `agent_started` | `version, previous_exit (clean\|unclean), os, hostname` |
| `agent_stopping` | `reason, uptime_s` (clean shutdown announcement) |
| `process_start` / `process_stop` | `pid, ppid, exe, user, rss_mb, rule_id` — only for processes matching signatures; no command lines |
| `process_snapshot` | `hash, count, added[], removed[]` (executable names) |
| `rule_hit` | `rule_id, severity, category, description, subject, actions[], result, context, mode, session_id, user` |
| `listening_port` | `port, address, process, pid, rule_id, probed` |
| `window_focus` | `app, title` (title filtered by policy: off / app_only / full) |
| `paste` / `copy` | `chars, lines, code, score, src, dst, user` |
| `key_stats` | per 5 s window: `keys, backspaces, iki_mean/median/p90/std, bursts, max_pause, paste_chords` |
| `net_adapter` | `change, name, driver, description, mac, addresses[], up, wireless, virtual, default_route, kind` |
| `device` | `action, subsystem, class, vendor, product, model, serial_hash` |
| `extension_scan` | `count, hash, ai[], added[], removed[]` |
| `artifact_scan` | `count, files_visited, truncated, found[{path, size_mb, rule_id}]` |
| `file_activity` | per work folder / 10 s: `saves, created, deleted, bytes_delta, total_bytes, exts[]` |
| `gpu_sample` | `util_pct, mem_used_mb, mem_total_mb` (only spikes) |
| `tamper` | `kind` ∈ binary_hash_mismatch, binary_changed, helper_binary_missing, config_changed, ca_removed, ca_changed, clock_change, helper_missing, signature_invalid |
| `helper_status` | `connected, user, session, caps` |
| `command_result` | `command_id, status, detail` |

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
| `kill` | `pid` or `exe` | terminate (exempt system processes are never killed) |
| `isolate_notice` | `isolated` | tell the student network access was paused/restored |
| `rescan` | `what` ∈ extensions, artifacts, processes, all | immediate scan |
| `ping`, `flush` | – | flush the queue now |

Every command is acknowledged with a `command_result` event.

## Helper ↔ service IPC
JSON lines over `/run/xephyguard/helper.sock` (Linux, peer checked with `SO_PEERCRED` + `/proc/<pid>/exe`)
or `\\.\pipe\XephyGuardHelper` (Windows, client image path + session id checked). See
`agent/core/include/xg/ipc_messages.hpp`.

## Proxy → CCC (internal)
`POST http://127.0.0.1:8000/api/internal/net-events` with `Authorization: Bearer $XG_INTERNAL_TOKEN`,
`{events:[{client_ip, host, path?, layer: sni|http|path|search-ai, action: blocked|allowed|rewritten, category?, matched?, ts}]}`.
