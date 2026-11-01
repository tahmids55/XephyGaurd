# Windows test checklist

The Windows adapters are compile-checked in CI (MinGW syntax check + MSVC build) but their
**behaviour must be verified on a real Windows 10/11 machine or VM**. Run this list for every release.
Record results (pass/fail, notes, Windows build) in `eval/results/windows-<ver>.md`.

Setup: a test CCC, a Windows 11 VM with a standard user `student` and an admin `labadmin`, the release
installer, and the dashboard open on the live view.

## Install and enrollment
- [ ] Silent install `XephyGuard-Setup.exe /S /KEY=… /CCC=… /CAFP=…` as admin completes without UI.
- [ ] PC appears as Pending within 10 s; Approve → tile turns green within 10 s.
- [ ] `sc qc XephyGuardAgent`: LocalSystem, AUTO_START; `sc qfailure`: restart actions present.
- [ ] `certutil -store Root` lists the lab CA; `%ProgramData%\XephyGuard` is not readable by `student`.
- [ ] Wrong `/CAFP` → enrollment refuses (fingerprint mismatch) and the log says why.

## Session helper and transparency
- [ ] Log in as `student`: tray icon with tooltip "Monitored by XephyGuard · mode: …" appears.
- [ ] Task Manager as `student`: ending `xephyguard-helper.exe` → it is relaunched within ~15 s; if it
      stays dead, `tamper.helper_missing` alert after 90 s.
- [ ] Fast user switching / RDP session: a helper runs in each active session.
- [ ] Login banner shows before sign-in.

## Detection (Test mode)
- [ ] Rename `timeout.exe` to `ollama.exe` and run it → `proc.ollama` alert within 10 s.
- [ ] Start a listener on 127.0.0.1:11434 → `port.ollama` alert.
- [ ] Create `%USERPROFILE%\.vscode\extensions\github.copilot-1.0.0` → `ext.copilot` after rescan.
- [ ] `wsl.exe` (if installed) → `vm.wsl` alert.
- [ ] Plug in a USB stick → device event; Android phone with USB tethering → `net.tether_usb` alert.
- [ ] Connect a VPN client (e.g. WireGuard) → `net.vpn_adapter` and `net.vpn_client` alerts.
- [ ] Focus changes appear in the PC drawer timeline (app name only by default).
- [ ] Copy text in Notepad → `copy` event with size; Ctrl+V into VS Code → `paste` event with size and
      "code" flag; **no text anywhere in the database** (check `events.data`).
- [ ] Typing → key-rate chart fills; `key_stats` contain counts only.
- [ ] Work folder `Documents\Lab` saves → `file_activity` events.

## Actions and commands
- [ ] Profile with `kill` for `proc.ollama` → process terminated, `result.kill.killed=true`.
- [ ] Dashboard "Message" → balloon notification on the PC; command status `ok`.
- [ ] Dashboard "Isolate" → PC loses network except the CCC; notification shown; restore works.

## Tamper
- [ ] As `student`: `sc stop XephyGuardAgent` → access denied.
- [ ] As admin: stop the service during a session → `tamper.agent_stopped` alert; service restarted by SCM if killed via Task Manager (`taskkill /F`) → `machine_silent` and `unclean_restart` alerts.
- [ ] As admin: change system time by 1 h → `tamper.clock_change`.
- [ ] As admin: edit `agent.toml` → `tamper.config_changed`.
- [ ] Replace `xephyguard-helper.exe` with another binary → `binary_hash_mismatch` on next check.

## Offline and resources
- [ ] Unplug the network for 2 minutes, generate events, reconnect → events arrive, no duplicates.
- [ ] Idle in Practice mode for 30 minutes: CPU < 1 %, working set < 30 MB, uplink < 5 MB/h
      (heartbeat `usage` on the PC drawer, or `eval/export/metrics.py overhead`).

## Uninstall
- [ ] Uninstall requires elevation; service removed; alert raised if a session was active.
