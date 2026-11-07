# Design decisions

| # | Decision | Why |
|---|---|---|
| 1 | Product name **XephyGuard** (spec says "LabGuard"); identifiers `xephyguard-*` | matches the repository/spec file name |
| 2 | **Laravel 13** + Reverb 1.12 instead of Laravel 11 | Laravel 11 is out of security support; `composer audit` showed unpatched advisories (TokenGuard auth bypass, XSS) fixed only in 12/13 |
| 3 | Lab sessions table is `lab_sessions` | `sessions` is Laravel's login-session table |
| 4 | Agent upstream = HTTPS batch POST; downstream commands = Reverb (Pusher protocol) WSS; both via nginx mTLS on :8443 | batches give offline buffering, de-dup by seq, schema validation; Reverb gives push |
| 5 | Agent auth = client cert (nginx-verified, fingerprint matched in PHP) **and** bearer token | a stolen token or cert alone is useless |
| 6 | Signature bundles signed with the CA key and verified by agents (EC P-256 / SHA-256) | integrity of pushed detections; PHP→C++ verification tested |
| 7 | Own minimal TLS/HTTP/WebSocket client on OpenSSL; nlohmann/json, toml++, Catch2 via FetchContent; raw kernel uevents instead of libudev | few runtime deps, small agent (RSS ≈ 15 MB in live test) |
| 8 | All CCC containers use **host networking**; Docker's iptables management disabled | real lab-PC source IPs for per-PC policy; no conflict with nftables |
| 9 | DNS = **Unbound** with per-client views | per-PC policy groups (test / block_ai / isolated / exempt) |
| 10 | SNI blocking in mitmproxy `tls_clienthello` with passthrough (no decryption); interception only for configured search hosts and restricted PCs | minimal decryption |
| 11 | mitmproxy signs with the lab root CA (key stays on the CCC) | intermediate-chain support in mitmproxy unverified; revisit |
| 12 | Windows installer = **NSIS** (`makensis` on Linux) | no Windows runner needed for packaging |
| 13 | Linux process starts: netlink proc connector + 2 s `/proc` diff | catches short-lived processes; works as fallback without root |
| 14 | Common dev ports (5001, 7860, 8000, 8080) need a positive LLM-API probe | false positive found in the live test |
| 15 | A mode change re-evaluates already-running processes, ports, adapters, extensions | live test: a tool opened before the Test was only logged |
| 16 | Scoring lives only in the Python analysis service (v1 rules, v2 IsolationForest opt-in) | single implementation; analysis tests use the real migration schema |
| 17 | Agents use their own API port for the Reverb connection (ignore the server-advertised port) | the CCC cannot see NAT/port-forwarding; found in the container test |
| 18 | When a batch ack announces a new policy version, the agent fetches it immediately | mode switches stay fast even if the realtime channel is down |
| 19 | Analysis SQL backtick-quotes column names (`keys`, `lines`, `usage`, `user`) | MySQL reserved words; SQLite tests could not catch it, found against MySQL 8.4 |
| 20 | mitmproxy runs as dedicated uid/gid 1500 with the CA at its default confdir | unprivileged proxy that can still read the 0600 CA bundle |
| 21 | nginx config uses `listen … ssl http2` (not `http2 on;`) and the image drops Debian's default site | compatible with nginx 1.22 in the Debian-based app image |
