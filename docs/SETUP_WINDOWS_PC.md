# Adding Windows lab PCs

1. Download `XephyGuard-Setup-<ver>.exe` from the **Add a PC** page on the CCC dashboard (after logging in as staff).
   The installer binary never contains embedded enrollment keys or CA fingerprints (`R10`); supply them at deploy time.
2. Install as administrator, either **silently** (recommended for fleet deployment) or **interactively**:
   * **Silent command line (GPO Startup Script, Microsoft Intune, PDQ Deploy, SCCM/MDT):**
     ```powershell
     XephyGuard-Setup-0.1.0.exe /S /ENROLLKEY=XG-XXXXXX-XXXXXX-XXXXXX /CAFP=<sha256-fingerprint> /CCC=192.168.10.2
     ```
     * **Active Directory GPO example:** Computer Configuration → Policies → Windows Settings → Scripts (Startup) → Script Name: `\\fileserver\xephyguard\XephyGuard-Setup-0.1.0.exe`, Script Parameters: `/S /ENROLLKEY=XG-XXXXXX-XXXXXX-XXXXXX /CAFP=<sha256> /CCC=192.168.10.2`.
     * **Microsoft Intune Win32 app example:** Package `XephyGuard-Setup-0.1.0.exe` with `IntuneWinAppUtil.exe`, set the install command to the silent command line above (run as **System**), and detect installation via `HKLM\SYSTEM\CurrentControlSet\Services\XephyGuardAgent`.
     * *(Note: `/KEY=` is also accepted as a backward-compatible alias for `/ENROLLKEY=`.)*
   * **Interactive fallback (single PC):** Run `XephyGuard-Setup-0.1.0.exe` as Administrator without arguments. The **Server Connection** wizard page prompts for the CCC IP/hostname, Lab Enrollment Key, and Server CA SHA-256 Fingerprint shown on the CCC **Add a PC** page.
   * For detailed GPO / Intune / PDQ / imaging guidance see [`installers/windows/README.md`](../installers/windows/README.md) and [`installers/gpo/README.md`](../installers/gpo/README.md).
3. The installer writes the CCC host and CA fingerprint to `%ProgramData%\XephyGuard\agent.toml` and registers the PC. The PC appears as **Pending** on the CCC dashboard → click **Approve**.

## What gets installed
* Service **XephyGuardAgent** (LocalSystem, automatic start, restart on failure; only administrators
  may stop it). The service starts `xephyguard-helper.exe` inside every user session with
  `CreateProcessAsUser` — there is no Run key or Startup shortcut to delete.
* Lab CA in `LocalMachine\Root`; `%ProgramData%\XephyGuard` (`agent.toml`, `signatures.json`, `manifest.sha256` — SYSTEM/Administrators ACL only).
* Outbound firewall rule for the agent; no inbound port.
* Managed Edge/Chrome/Firefox policies (Copilot sidebar, Gemini, DoH off), Windows Copilot off,
  login banner.
* Uninstall needs administrator rights; stopping the service during a session raises an alert.

## Hardening (GPO)
Standard user accounts; WSL / Hyper-V features disabled or monitored; removable-storage policy per lab;
VS Code with a read-only extensions directory.

**Before relying on Windows PCs, run [`docs/WINDOWS_TEST_CHECKLIST.md`](WINDOWS_TEST_CHECKLIST.md) on a real machine or VM.**
