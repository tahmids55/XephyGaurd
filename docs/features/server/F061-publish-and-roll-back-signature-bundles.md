# F61. Publish and roll back signature bundles

- **Component:** server
- **Area:** 11. Signatures and blocklists
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** compile and sign the bundle; activate an older version to roll back.
- **Trigger:** `POST /signatures/publish`, `POST /signatures/{version}/activate`; auto-publish [F110](F110-signature-auto-publish-every-minute.md).
- **Code:** `SignatureController::publish`, `activate`; `SignatureCompiler::publish`, `assertPublishable`.
- **Status:** working (`signature_publish_and_rollback`,
  `publish_refuses_bundle_without_domains_and_keeps_active_version`).
- **Test:** 1. Publish. 2. Activate the previous version. 3. Agents report the older version in heartbeat.
