# F42. Live score per PC

- **Component:** server
- **Area:** 7. Alerts and review
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** a running risk indicator on each seat from open alerts.
- **Code:** `EventIngestor::refreshLiveScore`, `reconcileLiveScores`.
- **Status:** working (`dashboard_reconciles_stale_live_score_when_no_open_alerts_exist`).
- **Test:** see [F41](F041-acknowledge-alerts-single-and-batch.md).
