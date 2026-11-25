# F9. Analysis scoring service (HTTP)

- **Component:** analysis (also: server)
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** separate Python service the CCC calls to score a session.
- **Trigger:** `python -m xg_analysis serve` (Docker service `analysis`, 127.0.0.1:8090);
  `GET /health`, `POST /score`.
- **Code:** `analysis/xg_analysis/service.py`, `pipeline.py`, `scoring.py`, `features.py`, `db.py`;
  CCC side `S/Services/AnalysisClient.php`.
- **In/out/side effects:** reads events from the DB (`XG_DB_URL`); returns scores and reasons.
- **Status:** working (analysis tests; `ending_a_session_survives_an_unreachable_analysis_service`).
- **Test:** 1. `cd analysis && python -m xg_analysis serve`. 2. `curl localhost:8090/health`.
