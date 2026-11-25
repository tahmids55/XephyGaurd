# F67. SNI blocking without decryption

- **Component:** proxy
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** the proxy closes TLS connections to blocked hosts from the ClientHello alone.
- **Code:** `proxy/addons/xephyguard_addon.py::tls_clienthello`.
- **Status:** working (`tls_clienthello_blocks_without_decryption`).
- **Test:** 1. In a Test session, `curl https://chatgpt.com` via the proxy fails at TLS.
