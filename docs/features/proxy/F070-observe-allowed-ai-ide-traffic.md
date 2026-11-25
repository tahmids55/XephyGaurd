# F70. Observe allowed AI/IDE traffic ⚠PRIV

- **Component:** proxy (also: server)
- **Area:** 12. Network enforcement (gateway, DNS, proxy)
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** when AI is allowed, the proxy still records host + path of AI/IDE requests and raises
  alerts.
- **Code:** addon `tls_clienthello`, `request`; `S/Http/Controllers/Internal/NetEventsController.php`.
- **Side effects:** `POST /api/internal/net-events` (internal token) → `net_events`, `alerts`.
- **Status:** working (`tls_clienthello_observes_allowed_ai_and_ide_traffic`,
  `observed_ai_and_ide_net_events_raise_alert_and_surface_in_dashboard`). Logs even when `log_urls`
  is off, and raises high-severity alerts while AI is allowed.
- **Test:** 1. Practice: AI allowed. 2. Open chatgpt.com on a PC. 3. A net event shows in the inspector.
