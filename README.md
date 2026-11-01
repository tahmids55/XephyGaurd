# XephyGuard

Controls and observes AI use on university lab computers while leaving students free to browse and
learn. One central computer (the **CCC**) manages every lab PC over the lab network: it blocks AI
services during tests, detects what cannot be blocked, and produces explainable, privacy-preserving
reports and research data. Specification: [`XephyGUARD_PROJECT_SPEC.md`](XephyGUARD_PROJECT_SPEC.md).

```
 lab PCs (Windows / Linux)                         CCC (Ubuntu, Docker Compose, host network)
 ┌──────────────────────────┐   TLS 1.3 mTLS :8443  ┌─────────────────────────────────────────────┐
 │ xephyguard-agent (root)  │──── events (HTTPS) ──▶│ nginx ─▶ Laravel 13 (API, dashboard, jobs)  │
 │  processes, ports, nets, │◀── commands (Reverb)──│          Reverb (WebSocket)  MySQL          │
 │  devices, extensions,    │                       │ analysis (Python: features, scores)         │
 │  models, files, tamper   │   all web traffic     │ netpolicy: Unbound DNS views + nftables     │
 │ xephyguard-helper (user) │───── via gateway ────▶│ proxy: mitmproxy (SNI filter, udm=14)       │
 │  window, clipboard size, │                       │ discovery (UDP 47474)                       │
 │  indicator, notices      │                       └─────────────────────────────────────────────┘
 └──────────────────────────┘                          ▲ instructor / assistant browser (HTTPS)
```

## Repository
| Path | What |
|---|---|
| `agent/` | C++20 agent + session helper: platform-independent core, Linux and Windows adapters, tests, CPack |
| `server/` | Laravel 13 CCC: agent API, dashboard, Reverb channels, scheduler, reports |
| `proxy/` | mitmproxy addon, DNS/nftables policy generator (`xg_netpolicy`), nftables + Unbound templates |
| `analysis/` | Python scoring service (v1 explainable rules, v2 Isolation Forest), research export |
| `ccc-deploy/` | Docker Compose stack, `install.sh`, `xephyguard-ccc` admin CLI (doctor, backup, update) |
| `installers/` | `.deb` scripts and systemd units, Windows NSIS installer, Ansible role, GPO notes, browser policies |
| `eval/` | red-team scenarios + harness, load simulator, metrics/paper tables, live checks |
| `data/` | seed signatures (YAML) and blocklists — never hard-coded in the agent |
| `docs/` | setup guides, protocol, threat model, ethics/consent, lab-assistant guide, decisions |

## Quick start
1. **CCC:** `sudo ./ccc-deploy/install.sh` on Ubuntu Server with two NICs → open `https://<ccc>/` →
   setup wizard → `sudo xephyguard-ccc doctor`. ([guide](docs/SETUP_CCC.md))
   Running the server on a **Windows** PC, or natively without Docker: [RUNNING_THE_SERVER.md](docs/RUNNING_THE_SERVER.md).
2. **Lab PCs:** dashboard → *Add a PC* → run the one-liner (Linux) or the installer (Windows) → click
   **Approve**. ([Linux](docs/SETUP_LINUX_PC.md), [Windows](docs/SETUP_WINDOWS_PC.md))
3. **Sessions:** start one from the live view or let the timetable do it.
   ([lab-assistant guide](docs/LAB_ASSISTANT_GUIDE.md))

## Status
| Component | State |
|---|---|
| Agent core + Linux adapters | implemented; 24 unit-test cases; live end-to-end tested against the CCC (native and containerized) |
| Windows adapters, service, helper, installer | implemented; compile-checked only — **run [the checklist](docs/WINDOWS_TEST_CHECKLIST.md) on real Windows** |
| CCC server + dashboard | implemented; 21 feature tests |
| Network layers (DNS, nftables, proxy) | implemented; 15 tests incl. mitmproxy addon; configs validated in containers |
| Analysis + research export | implemented; 7 tests on the real schema; verified against MySQL 8.4 |
| Docker stack | all 5 images build; app (nginx + php-fpm + MySQL 8.4 + Reverb + worker), analysis, DNS/nftables and proxy containers smoke-tested on an isolated Docker network |
| `install.sh` / gateway forwarding | written and syntax-checked; **not yet run on a real two-NIC host with lab PCs behind it** |
| Red-team harness, simulator, metrics | implemented; harness smoke-tested against a live local CCC |

See [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) for building and testing,
[`docs/PROTOCOL.md`](docs/PROTOCOL.md), [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md),
[`docs/ETHICS.md`](docs/ETHICS.md), [`docs/EVALUATION.md`](docs/EVALUATION.md) and
[`docs/DECISIONS.md`](docs/DECISIONS.md).

**Privacy:** never stored — typed characters, clipboard text, screen content, passwords, messages.
Every score is a *flag for human review* with traceable reasons.
