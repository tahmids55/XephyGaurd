# Windows agent installer

**Decision:** NSIS (builds on Linux with `makensis` or on Windows; no Windows runner is required for packaging).
A WiX MSI can be added later if an IT department requires MSI-only deployment; the silent parameters map 1:1 (`ENROLLKEY`, `CAFP`, `CCC`).

> [!IMPORTANT]
> **Zero embedded secrets (`R10`):** Never hardcode enrollment keys, CA fingerprints, or lab-specific secrets in `xephyguard.nsi` or in distributed installer binaries. Always supply them at deploy time via command-line parameters (`/ENROLLKEY=`, `/CAFP=`, `/CCC=`) or via the interactive setup page.

## Build

1. Build the agent with MSVC on Windows (or a Windows CI runner):
   ```powershell
   vcpkg install openssl sqlite3 --triplet x64-windows-static
   cmake -S agent -B agent/build-windows -G "Visual Studio 17 2022" -A x64 `
     -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
   cmake --build agent/build-windows --config Release
   ```
2. Compile the seed signatures next to the `.nsi`:
   ```bash
   python3 scripts/compile_signatures.py --out installers/windows/signatures.json
   ```
   *(Optional)* Place the lab's `ca.crt` next to the installer at deploy time (or bundle it for an internal-only image); otherwise enrollment downloads `/ca.crt` and verifies it against `/CAFP=`.
3. Package the installer:
   ```bash
   makensis -DVERSION=0.1.0 -DBUILD_DIR=../../agent/build-windows/Release installers/windows/xephyguard.nsi
   ```

`scripts/release.sh` runs steps 2–3 automatically when it finds the Windows build output.

## Deploy-time parameters

| Parameter | Description |
|---|---|
| `/S` | Silent (unattended) installation; skips all UI pages |
| `/ENROLLKEY=<key>` | Lab enrollment key from the CCC **Add a PC** page (required for enrollment; `/KEY=<key>` is also accepted as a backward-compatible alias) |
| `/CAFP=<sha256>` | Server CA SHA-256 fingerprint from the CCC **Add a PC** page (pins the lab CA during enrollment) |
| `/CCC=<address>` | CCC IP address or hostname (e.g., `192.168.10.2`); if omitted, the agent uses UDP broadcast auto-discovery (`47474/udp`) |

When run interactively without `/S` and `/ENROLLKEY=`, the installer displays a **Server Connection** wizard page prompting the administrator for the CCC address, enrollment key, and CA SHA-256 fingerprint, then writes them to `%ProgramData%\XephyGuard\agent.toml` and registers the PC as `Pending`.

## Enterprise deployment examples

| Method | Configuration |
|---|---|
| **Active Directory GPO** | Computer Configuration → Policies → Windows Settings → Scripts → Startup: `\\fileserver\xephyguard\XephyGuard-Setup-0.1.0.exe` with parameters `/S /ENROLLKEY=XG-XXXXXX-XXXXXX-XXXXXX /CAFP=<sha256> /CCC=192.168.10.2` (see `../gpo/README.md`) |
| **Microsoft Intune** | Package with `IntuneWinAppUtil.exe`; Install command: `XephyGuard-Setup-0.1.0.exe /S /ENROLLKEY=XG-XXXXXX-XXXXXX-XXXXXX /CAFP=<sha256> /CCC=192.168.10.2`; Uninstall command: `"C:\Program Files\XephyGuard\uninstall.exe" /S`; Install behavior: **System**; Detection rule: Registry `HKLM\SYSTEM\CurrentControlSet\Services\XephyGuardAgent` exists |
| **PDQ Deploy** | Install step with the silent command line above, run as **Deploy User (Interactive / Local Admin)** |
| **OS Imaging (MDT / SCCM)** | Run the silent command line in the task sequence; the agent writes `%ProgramData%\XephyGuard\agent.toml` and registers on first boot (remains **Pending** until approved on the CCC) |

Test the result with `docs/WINDOWS_TEST_CHECKLIST.md`.
