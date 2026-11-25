# F41. Acknowledge alerts (single and batch)

- **Component:** server
- **Area:** 7. Alerts and review
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff mark alerts reviewed, with a comment; the seat turns green again.
- **Trigger:** `POST /alerts/{alert}/ack`, `POST /alerts/ack-batch` (up to 500 IDs).
- **Code:** `AlertController::acknowledge`, `acknowledgeBatch`.
- **Status:** working (`acknowledging_all_alerts_turns_seat_green_and_resets_live_score`).
  ⚠SEC batch has no lab scoping and re-stamps already acknowledged alerts.
- **Test:** 1. Acknowledge all alerts for one PC. 2. Its seat turns green.
