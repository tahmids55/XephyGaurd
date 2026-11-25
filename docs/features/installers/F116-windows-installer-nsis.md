# F116. Windows installer (NSIS)

- **Component:** installers
- **Area:** 19. Packaging and deployment tools
- **Source:** [docs/FEATURES.md](../../FEATURES.md) (status legend, roles, `S/` = `server/app/`, `A/` = `agent/`)

- **What:** `XephyGuard-Setup-<ver>.exe`: installs agent + helper, service, CA and code-signing cert
  into Root, ACLs, tamper manifest; `/S /KEY=` silent install.
- **Code:** `installers/windows/xephyguard.nsi`, CI job `agent-windows-msvc`, `scripts/sign_binaries.ps1`.
- **Status:** unclear (never run on real Windows). ⚠SEC gives Users read access to the agent key;
  installs a self-signed cert as a root CA; unsigned build in CI.
- **Test:** only on a Windows VM.
