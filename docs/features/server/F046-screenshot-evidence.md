# F46. Screenshot evidence ⚠PRIV

- **Component:** server
- **Area:** 8. Screen viewing and evidence
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** staff save a still image of a PC's screen as evidence with a note.
- **Trigger:** `POST /pcs/{pc}/evidence` (png/webp/jpeg ≤ 8 MB, monitor, note);
  `GET /evidence/{capture}`. Assistant and up.
- **Code:** `S/Http/Controllers/Web/EvidenceController.php` (`store`, `show`).
- **In/out/side effects:** file at `storage/app/evidence/pc_<id>/<uuid>.<ext>`; `evidence_captures`
  row; audit. Purged with raw data ([F109](F109-retention-purge-daily-03-15.md)).
- **Status:** working (`instructor_can_upload_view_and_report_screen_evidence`,
  `invalid_file_is_rejected_and_retention_purges_expired_evidence`). Conflicts with ETHICS.md;
  any staff can upload any image (not tied to the stream).
- **Test:** 1. Open a PC's screen. 2. Click "Save evidence". 3. It shows in the session report.
