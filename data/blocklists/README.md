# Blocklists

| File | Applied in | Purpose |
|---|---|---|
| `ai_domains.txt` | Test (and Practice when the profile says "block AI") | Chatbots, AI search, LLM APIs |
| `ide_ai_backends.txt` | Test | Copilot / Cursor / Codeium / Tabnine backends |
| `marketplaces.txt` | Test | Extension marketplaces |
| `doh_dot.txt` | Always | DoH/DoT resolvers so DNS filtering cannot be bypassed |
| `doh_ips.txt` | Always | Resolver IPs dropped by nftables |
| `vpn_proxy.txt` | Test | VPN / Tor / web-proxy services |

Entries of the form `host/path` (e.g. `huggingface.co/chat`) are path rules: DNS cannot block them,
so they are enforced only by the TLS-inspecting proxy for hosts the CCC inspects (or ignored if the
client does not trust the CA). Pure domains are enforced by DNS **and** by SNI filtering.

These files are seeds. At runtime the CCC stores entries in the `blocklist_entries` table (editable,
versioned) and regenerates Unbound/mitmproxy/nftables configuration on every change.
