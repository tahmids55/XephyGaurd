# F33. Student notice in policy

- **Component:** server (also: agent)
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** the notice text (from settings/profile) is sent to PCs and shown to students.
- **Code:** `PolicyService` (`notice_text`), `S/Services/NoticeText.php`, helper ([F94](../agent/F094-monitoring-status-indicator.md)).
- **Status:** working. The admin text is followed by sentences generated from the enabled screen
  features (live view, screenshot evidence; both on when the screen-gateway secret is set), S-25.
  The old default's screen sentence is stripped from stored texts so it is not stated twice (`NoticeTextTest`).
- **Test:** 1. Change `notice_text` in Settings. 2. Log in on a lab PC: the new notice shows.
