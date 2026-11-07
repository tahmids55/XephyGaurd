# Security Policy

XephyGuard is an academic-integrity and lab-management system deployed in university computer labs. Because the agent runs as a privileged service (`LocalSystem` on Windows, `root` on Linux) and the Central Control Computer (**CCC**) manages lab network policy and student session metadata, security vulnerabilities in any component are treated with the highest priority.

---

## 1. Supported Versions

Security updates and patches are provided for the latest release branch and active pilot deployments:

| Version | Status | Security Updates |
|---|---|---|
| `0.1.x` (current Phase 0 / Phase 1 stabilization) | Active development | Supported (via `main` branch and point releases) |
| `< 0.1.0` (early prototypes) | End of life | Not supported — upgrade to the latest `0.1.x` release |

Once `1.0.0` reaches General Availability, the project will maintain security patches for the current major.minor release (`N`) and the immediately preceding minor release (`N-1`) for at least 12 months.

---

## 2. Reporting a Vulnerability

> [!IMPORTANT]
> **Do not report security vulnerabilities through public GitHub issues, discussions, or pull requests.**

Please report suspected vulnerabilities privately using one of the following channels:

* **Primary Security Contact (Email):** `[MAINTAINER PLACEHOLDER: insert security contact email, e.g. security@<your-domain-or-university.edu>]`
* **PGP Key Fingerprint (Optional):** `[MAINTAINER PLACEHOLDER: insert PGP key fingerprint or URL to public key]`
* **GitHub Private Vulnerability Reporting:** If enabled on the repository, open a private security advisory under the **Security → Advisories → Report a vulnerability** tab.

### What to include in your report
To help us triage and reproduce the issue quickly, please include:
1. **Component affected:** (`agent/` C++ service or session helper, `server/` Laravel CCC, `proxy/` mitmproxy/Unbound/nftables policy generator, `analysis/` scoring service, or `installers/`).
2. **Threat category:** (e.g., privilege escalation on a lab PC, remote code execution, mTLS/authentication bypass, CCC impersonation, cross-site scripting/CSRF on the dashboard, unauthorized access to student data, or stuck network isolation).
3. **Reproduction steps:** Clear, minimal proof-of-concept (PoC) steps, configuration details, and exact commit SHA or version tested.
4. **Impact assessment:** What an attacker (such as a standard non-admin student account on the lab LAN) can achieve and how it differs from the residual bypasses already documented in [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md).

---

## 3. Response Timelines & Disclosure Process

We follow a coordinated vulnerability disclosure process with the following target service levels:

| Stage | Target Timeline | Description |
|---|---|---|
| **Acknowledgment** | Within **48 hours** | Confirmation that your report was received and assigned a tracking ID |
| **Initial Triage & Validation** | Within **5 business days** | Reproduction of the issue, severity assessment (CVSS v3.1 / v4.0), and confirmation of affected versions |
| **Remediation Plan / Workaround** | Within **10 business days** | Target patch schedule shared with the reporter, plus immediate configuration mitigations if applicable |
| **Fix & Coordinated Release:** *Critical / High* | Within **14–30 days** | Patch developed, tested in CI, and distributed to pilot institutions prior to public advisory |
| **Fix & Coordinated Release:** *Medium / Low* | Within **30–60 days** | Included in the next scheduled point release |
| **Public Disclosure** | Up to **90 days** (or mutually agreed) | Security advisory published alongside the patched release, crediting the reporter (unless anonymity is requested) |

---

## 4. Scope: Security Vulnerabilities vs. Documented Monitoring Bypasses

XephyGuard maintains an open, honest threat model in [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md). Please distinguish between:

* **Security vulnerabilities (in scope for private disclosure under this policy):**
  * Local privilege escalation from a standard user account to `SYSTEM`/`root` via `xephyguard-agent`, `xephyguard-helper`, IPC pipes/sockets, or installer permissions.
  * Authentication bypass, SQL injection, remote code execution, SSRF, XSS, CSRF, or role-escalation on the CCC server (`:80`, `:443`, `:8443`).
  * Cryptographic or TLS verification flaws (such as CA pinning bypass, hostname/SAN verification bypass, or forged signature bundles).
  * Unauthorized exposure of student session records, audit logs, enrollment keys, or CA private keys.
  * Availability bugs that leave a student's PC permanently network-isolated (`R11`) or corrupt user files.
* **Evasion / AI-detection bypasses (documented in [`docs/THREAT_MODEL.md`](docs/THREAT_MODEL.md)):**
  * Novel ways to reach an AI assistant during a test without raising an alert (e.g., new AI domains, novel tunneling techniques) are welcomed! Unless they also involve a software security vulnerability from the list above, they may be submitted as red-team scenarios for `eval/scenarios/` or reported privately if you prefer coordinated rule updates.

---

## 5. Safe Harbor

We support independent security and privacy research conducted in good faith:
* We will not pursue legal action or institutional disciplinary referrals against researchers or students who test XephyGuard in their own local/isolated lab environments (such as the Docker/VM testbed described in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)) and report findings under this policy.
* **Do not** test against live university exam sessions, disrupt active classes, or access real students' data on a production CCC without explicit written authorization from the institution's IT/security team.
