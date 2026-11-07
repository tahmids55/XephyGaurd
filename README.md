# XephyGuard

Controls and observes AI use on university lab computers while leaving students free to browse and
learn. One central computer (the **CCC**) manages every lab PC over the lab network: it blocks AI
services during tests, detects what cannot be blocked, supports opt-in AI-permitted assessments with
transparent disclosure, and produces explainable, privacy-preserving reports and research data.
Specification: [`XephyGUARD_PROJECT_SPEC.md`](XephyGUARD_PROJECT_SPEC.md).

> **Naming consistency note:** The canonical product, binary, and documentation name is **XephyGuard**
> (spelled `G-u-a-r-d`). Some legacy Git remote URLs or repository slugs may appear as `XephyGaurd`;
> always use **XephyGuard** in documentation, code, packages, and institutional materials.

```
 lab PCs (Windows / Linux)                         CCC (Linux / Docker Compose or Windows)
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

## Supported Platforms

* **Central Control Computer (CCC):**
  * **Linux + Docker Compose (primary gateway stack):** Ubuntu Server with two NICs running the full containerized stack (Laravel 13, Reverb, MySQL 8.4, Python analysis, Unbound DNS views, nftables, and mitmproxy). See [`docs/SETUP_CCC.md`](docs/SETUP_CCC.md).
  * **Windows (supported server platform):** Native Windows CCC stack (PHP, nginx, MySQL/SQLite, Reverb, Python analysis, and network sync) for labs running Windows server infrastructure. See [`docs/RUNNING_THE_SERVER.md`](docs/RUNNING_THE_SERVER.md).
* **Lab PCs (Agents):**
  * **Windows 10/11 (64-bit):** Native C++20 `LocalSystem` service + per-session user helper, packaged as an NSIS installer supporting GPO/Intune silent deployment (`/S /ENROLLKEY=... /CAFP=... /CCC=...`). See [`docs/SETUP_WINDOWS_PC.md`](docs/SETUP_WINDOWS_PC.md).
  * **Linux (Debian/Ubuntu):** Native C++20 `systemd` service + user session helper, packaged as `.deb` with Ansible role support. See [`docs/SETUP_LINUX_PC.md`](docs/SETUP_LINUX_PC.md).

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
| `docs/` | charter, ethics/compliance, setup guides, protocol, threat model, roadmap, lab-assistant guide, decisions |

## Quick start
**Complete lab setup (server, installers, Linux and Windows PCs, acceptance test):
[`guide_setup.md`](guide_setup.md).**

1. **CCC:**
   * **Linux / Docker Compose:** `sudo ./ccc-deploy/install.sh` on Ubuntu Server with two NICs → open `https://<ccc>/` → setup wizard → `sudo xephyguard-ccc doctor`. ([guide](docs/SETUP_CCC.md))
   * **Windows or native non-Docker CCC:** Follow [`docs/RUNNING_THE_SERVER.md`](docs/RUNNING_THE_SERVER.md).
2. **Lab PCs:** dashboard → *Add a PC* → run the one-liner (Linux) or the installer with deploy-time parameters (Windows) → click **Approve**. ([Linux](docs/SETUP_LINUX_PC.md), [Windows](docs/SETUP_WINDOWS_PC.md))
3. **Sessions:** start one from the live view (`Practice`, `Test`, or opt-in `AI-assisted with disclosure`) or let the timetable do it. ([lab-assistant guide](docs/LAB_ASSISTANT_GUIDE.md))

## Status
| Component | State |
|---|---|
| Phase 0 Remediation (`R1`–`R12`) | **in progress** — enforcing [`docs/CHARTER.md`](docs/CHARTER.md) and [`docs/ETHICS.md`](docs/ETHICS.md) contracts across agent, server, and installers (see [`docs/ROADMAP.md`](docs/ROADMAP.md)) |
| Agent core + Linux adapters | implemented; unit-tested; live end-to-end tested against the CCC (native and containerized) |
| Windows adapters, service, helper, installer | implemented; deploy-time secret injection (`/ENROLLKEY=`, `/CAFP=`, `/CCC=`); **run [the checklist](docs/WINDOWS_TEST_CHECKLIST.md) on real Windows** |
| CCC server + dashboard | implemented (Linux/Docker and Windows supported); feature-tested |
| Network layers (DNS, nftables, proxy) | implemented; 15 tests incl. mitmproxy addon; configs validated in containers |
| Analysis + research export | implemented; 7 tests on the real schema; verified against MySQL 8.4 |
| Docker stack | all 5 images build; app (nginx + php-fpm + MySQL 8.4 + Reverb + worker), analysis, DNS/nftables and proxy containers smoke-tested on an isolated Docker network |
| `install.sh` / gateway forwarding | written and syntax-checked; **not yet run on a real two-NIC host with lab PCs behind it** |
| Red-team harness, simulator, metrics | implemented; harness smoke-tested against a live local CCC |

See [`docs/CHARTER.md`](docs/CHARTER.md), [`docs/ETHICS.md`](docs/ETHICS.md),
[`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md), [`docs/ROADMAP.md`](docs/ROADMAP.md),
[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md), [`docs/PROTOCOL.md`](docs/PROTOCOL.md),
[`docs/EVALUATION.md`](docs/EVALUATION.md), [`docs/DECISIONS.md`](docs/DECISIONS.md), and
[`SECURITY.md`](SECURITY.md).

**Privacy:** never stored — typed characters, clipboard text, screen content, passwords, messages.
Every score is a *flag for human review* with traceable reasons, and network isolation is fail-safe
(lease-backed with automatic route restoration).

## License and Commercial Support

* **Open-Source License ([`LICENSE`](LICENSE)):** XephyGuard is 100% open source and licensed under the
  **GNU Affero General Public License v3.0 (AGPL-3.0)**. Universities, ethics committees, instructors,
  and students are free to inspect, audit, modify, and self-host the entire platform (agent, server,
  gateway, and analysis service) with zero proprietary "open-core" black boxes.
* **Commercial Support, Deployment & Managed Services:** For universities and colleges seeking
  turnkey installation, campus SSO/LMS integration, custom DPIA/ethics-board preparation, SLAs, and
  priority security maintenance, paid support agreements and pilot Memoranda of Understanding (MoUs)
  are available from the maintainers:
  * **Commercial & Pilot Enquiries:** `[MAINTAINER PLACEHOLDER: insert support/pilot contact email or URL]`
