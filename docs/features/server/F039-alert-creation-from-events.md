# F39. Alert creation from events

- **Component:** server
- **Area:** 7. Alerts and review
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** rule hits, tamper events, paste over a size threshold, network events and machine
  silence become alerts, deduplicated.
- **Trigger:** agent `POST /api/agent/v1/events` (batches); proxy `POST /api/internal/net-events`.
- **Code:** `S/Services/EventIngestor.php::ingest`, `refreshLiveScore`; `S/Models/Alert.php`.
- **In/out/side effects:** writes `events`, `alerts`, `key_stats`, `paste_events`, `net_events`;
  updates `pcs` live score; broadcasts `AlertRaised`/`PcUpdated`; deletes older scan snapshots.
- **Status:** working (`batch_ingest_alerts_dedup_and_ack`,
  `test_mode_extension_and_local_model_rule_hits_create_alerts`).
- **Test:** 1. Start a Test session. 2. Open an AI app on a PC. 3. An alert appears on `/alerts`.
