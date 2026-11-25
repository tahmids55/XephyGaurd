# F15. Approve or reject enrollments

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff approve (optionally naming the PC) or reject pending PCs; "approve all".
- **Trigger:** `GET /enroll` (+ `.html`), `POST /enroll/{enrollment}/approve`,
  `POST /enroll/approve-all`, `POST /enroll/{enrollment}/reject`. Assistant and up.
- **Code:** `S/Http/Controllers/Web/EnrollmentController.php`, `EnrollmentService::approve/reject`.
- **In/out/side effects:** signs the client cert ([F3](F003-internal-certificate-authority.md)); creates or re-uses a `pcs` row (matched by
  machine_uid / MAC / hostname); revokes old `agents` rows and creates a new one with a token.
- **Status:** working (`auto_approve_by_mac_and_reenrollment_keeps_the_pc`,
  `cloned_machines_with_same_hostname_and_mac_stay_separate_pcs`,
  `legacy_pc_is_adopted_once_only_when_hostname_and_mac_match_uniquely`).
- **Test:** 1. Enroll a PC ([F14](../agent/F014-agent-enrollment-request.md)). 2. Approve on `/enroll`. 3. The agent finishes enrolling and the PC shows on the dashboard.
