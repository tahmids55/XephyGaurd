# F16. Auto-approve by MAC

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** admin turns on automatic approval and pastes a list of allowed MAC addresses; matching
  PCs are approved without a click.
- **Trigger:** `POST /enroll/auto-approve` (`auto_approve`, `macs`). Admin only.
- **Code:** `EnrollmentController::autoApprove`, `EnrollmentService`.
- **In/out/side effects:** writes `labs.auto_approve` and `labs.auto_approve_macs`; audit entry.
- **Status:** working (`auto_approve_by_mac_and_reenrollment_keeps_the_pc`). ⚠SEC with the key public
  ([F99](F099-downloads-and-install-commands.md)), a spoofed MAC gets a cert without a human.
- **Test:** 1. Turn it on. 2. Re-enroll a known PC: it is approved without clicking.
