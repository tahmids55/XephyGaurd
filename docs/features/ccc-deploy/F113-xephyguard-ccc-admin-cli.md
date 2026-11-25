# F113. `xephyguard-ccc` admin CLI

- **Component:** ccc-deploy
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** `doctor`, `status`, `logs [svc]`, `enroll-key`, `backup DIR` (DB dump + CA + settings),
  `update` (build, migrate, restart), `artisan …`.
- **Code:** `ccc-deploy/bin/xephyguard-ccc`.
- **Status:** unclear (only syntax-checked).
- **Test:** 1. `xephyguard-ccc status`. 2. `xephyguard-ccc backup /tmp/xg-backup` and check the files.
