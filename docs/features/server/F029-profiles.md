# F29. Profiles

- **Component:** server
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** list of profiles; admin creates, edits and deletes them (mode, collectors, intervals,
  actions, title capture, proxy options, notice text).
- **Trigger:** `GET /profiles` (+ `.html`, instructor and up); `POST /profiles/{profile?}`,
  `DELETE /profiles/{profile}` (admin).
- **Code:** `S/Http/Controllers/Web/ProfileController.php`, `S/Models/Profile.php`, `PolicyBuilder`.
- **In/out/side effects:** writes `profiles`; rewrites net state; audit.
- **Status:** partly working (saving tested by the udm14 test; 5 built-ins exist; the `google_udm14`
  default flipped to off, `docs/HISTORY.md` batch 5).
- **Test:** 1. Edit a profile's scan interval. 2. A PC in that profile gets the new interval (agent log).
