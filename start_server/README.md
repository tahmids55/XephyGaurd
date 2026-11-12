# XephyGuard Daily Server Run / Stop / Status Scripts

Day-to-day scripts to **start**, **restart**, **stop**, and **inspect** the XephyGuard CCC server once first-time setup from [`../new_setup_scripts/`](../new_setup_scripts/README.md) has already been run.

No IPs, drive paths, or credentials are hardcoded — all scripts locate the repository relative to their own folder and read ports/secrets from `.env` or `.env.local` dynamically.

---

## Directory Layout

```text
start_server/
├── README.md
├── linux/
│   ├── _common.sh          # Shared helper functions (sourced automatically)
│   ├── start-server.sh     # Start or restart server (Docker local, Docker prod, or Native)
│   ├── stop-server.sh      # Gracefully stop running server without deleting DB/CA data
│   └── status-server.sh    # Print live container/port status, URLs, and CA SHA-256 fingerprint
└── windows/
    ├── _common.ps1         # Shared PowerShell helpers
    ├── start-server.bat    # Double-click launcher (calls start-server.ps1)
    ├── start-server.ps1    # Start Nginx, Laravel, Reverb, Screen Gateway, & Scheduler
    ├── stop-server.bat     # Double-click stopper (calls stop-server.ps1)
    ├── stop-server.ps1     # Cleanly terminate XephyGuard server processes
    └── status-server.ps1   # Display listening ports, scheduler PID, URLs, & CA fingerprint
```

---

## Linux Commands

Make scripts executable once after cloning (only needed on filesystems without execute bits):

```bash
chmod +x start_server/linux/*.sh
```

| Task | Command |
|---|---|
| **Start server** (auto-detects Docker local, Docker prod, or Native) | `./start_server/linux/start-server.sh` |
| **Restart server** | `./start_server/linux/start-server.sh --restart` |
| **Stop server** | `./start_server/linux/stop-server.sh` |
| **Check live status & URLs** | `./start_server/linux/status-server.sh` |
| **Rebuild Docker images & restart** | `./start_server/linux/start-server.sh --mode docker --restart --rebuild` |
| **Force native mode on custom ports** | `./start_server/linux/start-server.sh --mode native --app-port 8000 --reverb-port 8080 --gateway-port 8082` |

### Mode Auto-Detection Order (Linux)
When `--mode auto` (the default) is used:
1. Active running containers (`xephyguard-local-*` -> `docker`; `ccc-deploy-*` / `xephyguard-web` -> `prod`), or running native PIDs.
2. Configured environment files (`ccc-deploy/.env.local` -> `docker`; `/opt/xephyguard/ccc-deploy/.env` or `ccc-deploy/.env` -> `prod`; `server/.env` -> `native`).

You can always pass `--mode docker`, `--mode prod`, or `--mode native` explicitly.

---

## Windows Commands

### Double-Click Launchers
- **Start / Ensure Running:** Double-click [`windows/start-server.bat`](windows/start-server.bat)
- **Stop All Services:** Double-click [`windows/stop-server.bat`](windows/stop-server.bat)

### PowerShell CLI

```powershell
# Start all services (or report status if already running)
.\start_server\windows\start-server.ps1

# Restart cleanly
.\start_server\windows\start-server.ps1 -Restart

# Check live service ports, Dashboard/LAN URLs, & CA fingerprint
.\start_server\windows\status-server.ps1

# Stop all services
.\start_server\windows\stop-server.ps1
```

**Optional Flags (`windows/start-server.ps1`):**
- `-Restart` (stops running XephyGuard processes before starting)
- `-AppPort`, `-ReverbPort`, `-GatewayPort`, `-HttpPort`, `-HttpsPort`, `-MtlsPort`
- `-NginxDir <PATH>`
- `-SkipNginx` (run HTTP direct without starting Nginx)

---

## Troubleshooting

- **Ports already in use:** Run `status-server.sh` / `status-server.ps1` first; pass `-Restart` (Windows) or `--restart` (Linux) to recycle processes cleanly.
- **Screen viewer relay fails (`screen-gateway` down):**
  - On **Windows**, install Go or keep `xephyguard-screen\gateway\gateway.exe` built (`start-server.ps1` auto-rebuilds when `go` is installed and `.go` sources change).
  - On **Linux native**, ensure `server/storage/app/bin/xephyguard-screen-gateway` was built during `setup-server-native.sh` (or use Docker mode where `screen-gateway` runs in its own container).
- **IP changed after moving PCs or DHCP lease renewal:**
  - Re-run `new_setup_scripts/linux/setup-server-docker.sh` or `new_setup_scripts/windows/setup-server.ps1` to regenerate certificates/config for the new LAN IP without wiping your database.
