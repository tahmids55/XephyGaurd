# Adding Windows lab PCs

1. Download `XephyGuard-Setup-<ver>.exe` from the **Add a PC** page.
2. Install, either interactively (as administrator) or silently:
   ```
   XephyGuard-Setup.exe /S /KEY=XG-XXXXXX-XXXXXX-XXXXXX /CCC=192.168.10.2 /CAFP=<sha256>
   ```
   For GPO / Intune / PDQ / imaging see `installers/windows/README.md` and `installers/gpo/README.md`.
3. The PC appears as **Pending** → **Approve** on the dashboard.

## What gets installed
* Service **XephyGuardAgent** (LocalSystem, automatic start, restart on failure; only administrators
  may stop it). The service starts `xephyguard-helper.exe` inside every user session with
  `CreateProcessAsUser` — there is no Run key or Startup shortcut to delete.
* Lab CA in `LocalMachine\Root`; `%ProgramData%\XephyGuard` (SYSTEM/Administrators only).
* Outbound firewall rule for the agent; no inbound port.
* Managed Edge/Chrome/Firefox policies (Copilot sidebar, Gemini, DoH off), Windows Copilot off,
  login banner.
* Uninstall needs administrator rights; stopping the service during a session raises an alert.

## Hardening (GPO)
Standard user accounts; WSL / Hyper-V features disabled or monitored; removable-storage policy per lab;
VS Code with a read-only extensions directory.

**Before relying on Windows PCs, run `docs/WINDOWS_TEST_CHECKLIST.md` on a real machine or VM.**
