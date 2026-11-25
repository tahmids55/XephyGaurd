# F45. Screen gateway (relay)

- **Component:** screen
- **Area:** 8. Screen viewing and evidence
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** Go WebSocket relay between agent streams and browser viewers.
- **Trigger:** `xephyguard-screen/gateway` binary (`-addr`, `-secret`, `-issuer`, `-audience`,
  `-trusted-proxy`, `-dev-insecure`); endpoints `/agent`, `/view`, `/health` (also under `/screen-gw/`).
- **Code:** `xephyguard-screen/gateway/main.go`, `relay.go`, `auth.go`.
- **Status:** partly working (Go tests exist but are not run in CI or here; tracked `gateway.exe` is
  stale). ⚠SEC Origin check off; agents authenticate only by the nginx-verified mTLS cert.
- **Test:** 1. `cd xephyguard-screen/gateway && go run . -secret x`. 2. `curl localhost:8082/health`.
