# F17. Rotate enrollment key (web)

- **Component:** server
- **Area:** 3. Enrollment and PC management
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** admin replaces the enrollment key.
- **Trigger:** `POST /enroll/rotate-key`. Admin only.
- **Code:** `EnrollmentController::rotateKey`.
- **Status:** working (`key_rotation_invalidates_old_key`).
- **Test:** 1. Rotate. 2. Enrolling with the old key fails.
