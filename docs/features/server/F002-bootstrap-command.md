# F2. Bootstrap command

- **Component:** server
- **Area:** 1. CCC setup and operations
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** seeds the lab, the 5 built-in profiles, blocklists and rules from `data/`, publishes
  signatures and writes network state.
- **Trigger:** `php artisan xephyguard:bootstrap`.
- **Code:** `S/Console/Commands/BootstrapCommand.php`, `S/Services/Bootstrap.php::run`,
  `BlocklistService::importSeed`, `SignatureCompiler::importSeed/publish`.
- **In/out/side effects:** reads `data/blocklists/*.txt`, `data/signatures/*.yaml`; writes `labs`,
  `profiles`, `blocklist_entries`, `rules`, `signature_versions`; writes `state.json`.
- **Status:** working (`bootstrap_publishes_bundle_with_ai_and_ide_domains`).
- **Test:** 1. `cd server && php artisan xephyguard:bootstrap`. 2. `/profiles` lists 5 profiles;
  `/signatures` shows an active version.
