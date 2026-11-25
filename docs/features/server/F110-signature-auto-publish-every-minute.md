# F110. Signature auto-publish — every minute ⚠SEC

- **Component:** server
- **Area:** 18. Background jobs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> Run by `php artisan schedule:work` (Docker service `scheduler`) and `queue:work` (service `worker`).
> Defined in `server/routes/console.php`.

**Feature:**

- **What:** compiles and publishes a new bundle when rules/blocklists changed (no-op otherwise).
- **Code:** `server/routes/console.php:11-20`, `SignatureCompiler::publish(null, 'auto')`.
- **Status:** working (`publish_is_noop_when_nothing_changed`). No review step.
- **Test:** 1. Toggle a rule. 2. Within a minute `/signatures` shows a new active version.
