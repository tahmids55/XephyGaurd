# Group Policy deployment notes (Windows lab PCs)

1. Put `XephyGuard-Setup-<ver>.exe` on a network share readable by *Domain Computers* (restrict read access to lab OUs/security groups so the enrollment key in startup script arguments is not exposed outside the lab).
2. GPO → Computer Configuration → Policies → Windows Settings → Scripts (Startup) → add
   `\\fileserver\xephyguard\XephyGuard-Setup-<ver>.exe` with parameters:
   ```
   /S /ENROLLKEY=<lab key> /CAFP=<CA SHA-256> /CCC=<CCC IP>
   ```
   Re-running is harmless (upgrades in place while preserving an already-approved PC identity).
3. Recommended lab hardening in the same GPO:
   * Lab users are standard users (no local admin).
   * Computer Configuration → Administrative Templates → Windows Components → **Windows Copilot**: Turn off.
   * Import the Edge and Chrome ADMX templates and set the policies listed in
     `installers/browser-policies/README.md` (the installer also writes them to the registry).
   * Disable the *Windows Subsystem for Linux* and *Virtual Machine Platform* optional features, or
     leave them and rely on the `vm.*` detection rules.
   * Removable storage: per lab policy (`All Removable Storage classes: Deny all access` for exams).
   * VS Code: deploy a machine-wide installation with a read-only extensions directory
     (`code --extensions-dir C:\ProgramData\XephyGuard\vscode-extensions`, ACL read-only for users).
4. Do **not** deploy a proxy setting: in gateway mode the CCC intercepts traffic transparently, so
   there is nothing to bypass on the PC.
