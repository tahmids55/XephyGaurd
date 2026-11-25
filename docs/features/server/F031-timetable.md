# F31. Timetable

- **Component:** server
- **Area:** 5. Sessions, profiles and timetable
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** sessions start and stop on a weekly schedule.
- **Trigger:** `POST /timetable`, `POST /timetable/{entry}/toggle`, `DELETE /timetable/{entry}`
  (instructor and up); cron [F107](F107-timetable-tick-every-minute.md).
- **Code:** `ProfileController` (`saveEntry`, `toggleEntry`, `deleteEntry`), `SessionManager::timetableTick`.
- **In/out/side effects:** writes `timetable_entries`; starts/stops `lab_sessions`.
- **Status:** working (`timetable_starts_and_stops_sessions`).
- **Test:** 1. Add an entry starting in 2 minutes. 2. Keep the scheduler running. 3. A session starts on time.
