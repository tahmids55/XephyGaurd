# Plan: ccc-deploy/ (Docker Compose, install.sh, xephyguard-ccc CLI, discovery)

Sources: [audit/ccc-deploy.md](../audit/ccc-deploy.md) (I-CCC-1..31), [features/ccc-deploy/](../features/ccc-deploy/README.md) (F8, F112, F113, F114).
Plan written 2026-11-17. Nothing run while planning. `installers/` and `scripts/` are out of scope.

Rules for every step: one component, <300 changed lines, one kind of change, then
`for f in ccc-deploy/*.sh ccc-deploy/docker/*.sh ccc-deploy/bin/xephyguard-ccc; do bash -n "$f"; done`, `shellcheck` on touched files, and
`docker compose -f ccc-deploy/docker-compose.yml config -q` (needs `ccc-deploy/.env`). A full `up --build` is heavy on the 8 GB machine: ask first.
install.sh and gateway forwarding were never run on a real two-NIC host, so every step here is "config/syntax-checked only" unless you run it on one.
**[APPROVAL]** = changes a documented feature or DECISIONS entry; **[CROSS]** = another component's plan has the matching step.

## Phase 1: Critical/High security and bugs
- [x] Step 1: Add the `screen-gateway` service to the production compose, listening on `127.0.0.1:8082` only; nginx `/screen-gw/` (mTLS/TLS) is the only public path (fixes I-CCC-7; touches F112) [CROSS: screen]
- [ ] Step 2: install.sh and `.env.local` stop opening `:8082` on all interfaces; `SCREEN_GATEWAY_URL` = `wss://<ccc>:8443/screen-gw` so `PolicyBuilder` sends `tls=true` (fixes I-CCC-1; touches F112) [CROSS: server Step 3, agent Step 9; docs/PROTOCOL.md]
- [ ] Step 3: mitmproxy and Unbound bind the lab-side address, not `0.0.0.0`; `block_global=true`; `proxy_only` mode gets a minimal WAN input drop (fixes I-CCC-4; touches F112) [CROSS: proxy Step 3]
- [ ] Step 4: `xephyguard-ccc backup` uses `set -e`, checks `mysqldump`, writes with `umask 077` before creating files (fixes I-CCC-5, I-CCC-8; touches F113)
- [x] Step 5: install.sh prints a one-time setup token and passes it as `XG_SETUP_TOKEN` (fixes I-CCC-6; touches F112) [CROSS: server Step 6] (done with server S-9; install.sh not run)
- [ ] Step 6: nginx serves `/install/linux.sh` over TLS (self-signed lab CA) with documented `curl --cacert`/pinned-key use; the CA fingerprint is printed at install for out-of-band checking (fixes I-CCC-2; touches F112, F8) [CROSS: server Step 25]
- [ ] Step 7: Dedicated interception CA for mitmproxy; the lab root `ca.key` is no longer mounted in the proxy container (fixes I-CCC-3, I-CCC-15; touches F112, F114) [APPROVAL: reverses DECISIONS #11; update DECISIONS.md and THREAT_MODEL.md]

## Phase 2: Tooling
- [ ] Step 8: shellcheck clean for `ccc-deploy/*.sh`, `docker/*.sh`, `bin/xephyguard-ccc` (quoting fixes only, no behavior change) (fixes none; touches F112, F113)
- [ ] Step 9: `ruff` clean for `ccc-deploy/services/*.py` (fixes none; touches F8)
- [ ] Step 10: Add shellcheck to CI (needs approval to edit CI) (fixes none) [APPROVAL]

## Phase 3: Config and secrets
- [ ] Step 11: Per-service `env_file`/`environment`: DB root password only to MySQL and the one-shot migrate job; `APP_KEY` only to app containers; discovery and proxy get none (fixes I-CCC-13; touches F112)
- [ ] Step 12: MySQL password from `MYSQL_PWD` env or a `--defaults-extra-file` (0600), not `-p…` on command lines or healthcheck (fixes I-CCC-20; touches F112, F113)
- [ ] Step 13: install.sh writes `.env` with proper quoting/escaping; reading is `grep`/parse, not `source` (fixes I-CCC-21; touches F112, F113)
- [ ] Step 14: Network mode single source: entrypoint reads `network_mode` from `state.json`, `XG_NETWORK_MODE` is only a default (fixes I-CCC-9; touches F112) [CROSS: proxy F64]
- [ ] Step 15: IP forwarding only in `gateway` mode, also in `doctor` (fixes I-CCC-29; touches F112, F113)
- [ ] Step 16: DNS fallback resolver configurable and off by default (no `8.8.8.8` leak) (fixes I-CCC-28; touches F112)
- [ ] Step 17: Merge (not overwrite) `/etc/docker/daemon.json` using `jq`-free safe merge, backup first (fixes I-CCC-17; touches F112)
- [ ] Step 18: `cap_drop: [ALL]`, `no-new-privileges`, `read_only` + tmpfs for proxy, discovery, screen-gateway (fixes I-CCC-30; touches F112)
- [ ] Step 19: Analysis binds `127.0.0.1` via `XG_ANALYSIS_BIND`, comment corrected (fixes I-CCC-18; touches F112) [CROSS: analysis Step 2]
- [ ] Step 20: Pin base images by tag + digest and `GOTOOLCHAIN=local` (fixes I-CCC-14 part; touches F112)

## Phase 4: Structure (moves only)
No move-only items in the audit.

## Phase 5: Duplication and dead code
- [ ] Step 21: One-shot `migrate` service (`service_completed_successfully` dependency); remove `migrate --force` from the web entrypoint and from the duplicate calls in install.sh/`update` (fixes I-CCC-24; touches F112, F113)

## Phase 6: Errors, logging, validation
- [ ] Step 22: Discovery responder validates the datagram is a JSON object and `ca.crt` parses; never crashes (fixes I-CCC-12; touches F8)
- [ ] Step 23: Discovery binds the lab interface only, same-size reply, per-source rate limit (fixes I-CCC-19; touches F8)
- [ ] Step 24: MySQL wait loop fails after the timeout with a clear message (fixes I-CCC-16; touches F112)
- [ ] Step 25: Supervise the netpolicy watcher (one process manager or healthcheck that fails when the watcher exits) (fixes I-CCC-11; touches F112, F113)
- [ ] Step 26: `doctor`: verify dashboard TLS against the lab CA (no `curl -k`); DoH sinkhole check fails when `dig` gives nothing (fixes I-CCC-22, I-CCC-23; touches F113)
- [ ] Step 27: `import-sqlite-to-local.php`: TRUNCATE inside a safe sequence (create-new-then-swap or fail before truncate) (fixes I-CCC-31; touches F114)

## Phase 7: Medium/Low bugs
- [ ] Step 28: `xephyguard-ccc artisan` runs as `www-data` (fixes I-CCC-10; touches F113)
- [ ] Step 29: `update` takes a backup first and aborts if it fails (fixes I-CCC-25; touches F113)
- [ ] Step 30: Backup includes `/var/lib/xephyguard/storage` and the mitmproxy CA bundle; restore doc updated (fixes I-CCC-26; touches F113)
- [ ] Step 31: Partition maintenance: monthly event/job that adds the next partitions (fixes I-CCC-27; touches F112) [CROSS: server scheduler if done in PHP]

## Phase 8: Measured performance
No performance finding in the audit.

## Phase 9: Dependency updates
- [ ] Step 32: After Step 20, update base image digests one image per step with `docker compose build` and the server test suite (fixes I-CCC-14 rest; touches F112)
