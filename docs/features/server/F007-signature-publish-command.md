# F7. Signature publish command

- **Component:** server
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** compiles, signs and activates the signature bundle from the DB (optionally importing
  new rules from `data/signatures` first).
- **Trigger:** `php artisan xephyguard:signatures:publish [--import]`.
- **Code:** `S/Console/Commands/PublishSignatures.php`, `S/Services/SignatureCompiler.php`.
- **In/out/side effects:** writes `signature_versions`; agents fetch it with their policy.
- **Status:** working (`signature_publish_and_rollback`, `publish_is_noop_when_nothing_changed`).
- **Test:** 1. Run it. 2. `/signatures` shows a new active version (or "no change").
