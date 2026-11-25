# F14. Agent enrollment request

- **Component:** agent (also: server)
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** a lab PC asks to join the lab with the enrollment key and a CSR.
- **Trigger:** agent `xephyguard-agent enroll --key <key> [--ccc host] [--ca-fingerprint] [--trust-on-first-use] [--no-wait] [--timeout]`
  → `POST /api/agent/v1/enroll` (30/min), then polls `GET /api/agent/v1/enroll/{enrollment}` (120/min).
- **Code:** agent `A/apps/agent_main.cpp`, `A/core/src/enroll.cpp`; server
  `S/Http/Controllers/Agent/EnrollController.php`, `S/Services/EnrollmentService.php` (`register`, `poll`).
- **In/out/side effects:** writes `enrollments` (hostname, MACs, machine_uid, OS, CSR, IP);
  supersedes older pending requests for the same host. On approval the agent receives its cert,
  token and CA and stores them in its data dir.
- **Status:** working (`approve_dont_configure_flow`, `enroll_api_rejects_malformed_machine_uid`).
- **Test:** 1. On a PC: `sudo xephyguard-agent enroll --key <key> --ccc <ip>`. 2. It appears on `/enroll`.
