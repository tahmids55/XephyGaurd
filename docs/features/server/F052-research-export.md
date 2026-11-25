# F52. Research export

- **Component:** server
- **Area:** 9. Scoring, reports and research
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** pseudonymised export of consenting students only.
- **Trigger:** `GET /research/export` (instructor and up). The analysis CLI export was removed (analysis Step 13, DECISIONS #30).
- **Code:** `ReportController::research` → `App\Services\ResearchExport`; audit.
- **Status:** working (`ResearchExportTest`). CSV only; always the pseudonym.
- **Test:** 1. Give one student consent ([F55](F055-research-consent-per-student.md)). 2. Export: only that student, pseudonymised.
