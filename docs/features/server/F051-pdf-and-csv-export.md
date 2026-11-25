# F51. PDF and CSV export

- **Component:** server
- **Area:** 9. Scoring, reports and research
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /reports/{session}/pdf`, `GET /reports/{session}/csv`.
- **Code:** `ReportController::pdf` (dompdf, `resources/views/reports/pdf.blade.php`), `csv`; audit.
- **Status:** working (PDF rendered in the report test; CSV not tested).
- **Test:** 1. Download both. 2. The PDF opens; the CSV has one row per student.
