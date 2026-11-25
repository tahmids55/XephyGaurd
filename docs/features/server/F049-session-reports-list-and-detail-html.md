# F49. Session reports list and detail (HTML)

- **Component:** server
- **Area:** 9. Scoring, reports and research
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **Trigger:** `GET /reports` (+ `.html`), `GET /reports/{session}`, `GET /report-session-{num}.html`.
  Instructor and up.
- **Code:** `S/Http/Controllers/Web/ReportController.php` (`index`, `show`, `showByNumber`).
- **Status:** partly working (`report_show_and_pdf_render_when_blocked_net_events_exist`). The HTML
  report dropped signal IDs, review comments and "not observable", shows an inverted
  "integrity" score that differs from PDF/CSV, and has a hard-coded `+5`.
- **Test:** 1. Open a report. 2. Compare a student's number with the PDF ([F51](F051-pdf-and-csv-export.md)): they differ.
