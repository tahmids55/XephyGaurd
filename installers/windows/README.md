# Windows agent

**Decision:** NSIS (builds on Linux with `makensis`, no Windows runner needed for packaging).
A WiX MSI can be added later if the IT department requires MSI-only deployment; the silent
parameters would map 1:1 (`ENROLL_KEY`, `CCC_ADDR`).

## Build

1. Build the agent with MSVC on Windows (or a Windows CI runner):
   ```powershell
   vcpkg install openssl sqlite3 --triplet x64-windows-static
   cmake -S agent -B agent/build-windows -G "Visual Studio 17 2022" -A x64 `
     -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
   cmake --build agent/build-windows --config Release
   ```
2. Copy `signatures.json` (`scripts/compile_signatures.py --out installers/windows/signatures.json`)
   and the lab `ca.crt` (optional; enrollment can fetch and pin it) next to the `.nsi`.
3. `makensis -DVERSION=0.1.0 -DBUILD_DIR=../../agent/build-windows/Release installers/windows/xephyguard.nsi`

`scripts/release.sh` does steps 2–3 when it finds the Windows build output.

## Deploy

| Method | How |
|---|---|
| GPO | Computer Configuration → Policies → Windows Settings → Scripts → Startup: `\\server\share\XephyGuard-Setup.exe /S /KEY=XG-... /CCC=192.168.10.2 /CAFP=<sha256>` (see `../gpo/README.md`) |
| Intune | Win32 app (`IntuneWinAppUtil`), install command as above, detection: service `XephyGuardAgent` exists |
| PDQ Deploy | Install step with the silent command line, run as Deploy User (admin) |
| Imaging | Run the silent install in the task sequence; the agent enrolls on first boot (pending until approved) |

Test the result with `docs/WINDOWS_TEST_CHECKLIST.md`.
