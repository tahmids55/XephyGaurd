# F109. Retention purge — daily 03:15

- **Component:** server
- **Area:** 18. Background jobs
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

**Area context:**

> Run by `php artisan schedule:work` (Docker service `scheduler`) and `queue:work` (service `worker`).
> Defined in `server/routes/console.php`.

**Feature:**

- **What:** deletes raw events, key stats, paste events, net events and evidence images older than
  `retention_days_raw`, and reports older than `retention_days_reports`.
- **Code:** `S/Jobs/PurgeRetention.php`.
- **Status:** working (`…retention_purges_expired_evidence`).
- **Test:** 1. `php artisan tinker --execute="dispatch_sync(new App\Jobs\PurgeRetention)"` on a copy of the DB.
