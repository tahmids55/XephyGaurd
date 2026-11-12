# XephyGuard First-Time Setup Scripts

Use this directory **only when provisioning a new machine** (teacher CCC server or student lab PCs).  
For day-to-day start, stop, restart, and health checks after setup completes, use [`../start_server/`](../start_server/README.md).

Every script computes paths relative to the repository root and auto-detects the machine's LAN IP, subnet, timezone, and installed runtimes so the repository works across any Linux or Windows PC.

---

## Directory Layout

```text
new_setup_scripts/
├── README.md
├── linux/
│   ├── setup-server-docker.sh   # Recommended Linux CCC setup (Docker local stack or production host)
│   ├── setup-server-native.sh   # Non-Docker Linux CCC setup (PHP + SQLite + Reverb + Screen Gateway [+ optional Nginx])
│   └── install-agent.sh         # Standalone Linux (Ubuntu/Debian) student PC installer & enrollment
└── windows/
    ├── setup-server.bat         # Double-click launcher (runs setup-server.ps1 as Administrator)
    ├── setup-server.ps1         # First-time Windows CCC setup (PHP 8.3+, Composer/Vite, Nginx, CA, SQLite, Firewall)
    └── deploy-agents.ps1        # Mass WinRM installer for Windows student PCs on the LAN
```

---

## 1. Linux Server Setup

### Option A: Docker Mode (Recommended)

```bash
chmod +x new_setup_scripts/linux/*.sh start_server/linux/*.sh
./new_setup_scripts/linux/setup-server-docker.sh
```

What `--mode local` (the default) does automatically on any fresh Linux workstation:
1. Detects the primary LAN IP, subnet CIDR, network interface, and system IANA timezone + SQL offset.
2. Creates `ccc-deploy/.env.local` with freshly generated `APP_KEY`, database passwords, Reverb credentials, `XG_INTERNAL_TOKEN`, and Screen Gateway secrets (or adapts IPs cleanly if copied to another PC).
3. Builds Docker images if not already present, starts MySQL, initializes or seeds the Lab CA and `mitmproxy` CA, runs migrations, imports `server/database/database.sqlite` if present (or runs `php artisan xephyguard:bootstrap` on a fresh clone), writes network state, and launches the full stack via `start_server/linux/start-server.sh --mode docker`.

**Dedicated Ubuntu Lab Server (Production `gateway` or `proxy_only`):**

```bash
sudo ./new_setup_scripts/linux/setup-server-docker.sh --mode proxy_only
# or gateway router mode (2 NICs):
sudo ./new_setup_scripts/linux/setup-server-docker.sh --mode gateway --lan-if eth1 --wan-if eth0 --subnet 192.168.10.0/24 --ccc-ip 192.168.10.1
```

**Common Flags (`setup-server-docker.sh`):**
- `--mode local|proxy_only|gateway` (default: `local`)
- `--ip <IP>`, `--subnet <CIDR>`, `--lan-if <IFACE>`, `--wan-if <IFACE>`
- `--tz <IANA_TIMEZONE>` (e.g. `UTC`, `Asia/Dhaka`)
- `--lab-name "<NAME>"`
- `--rebuild` (rebuild images), `--reseed` (re-import `database.sqlite`), `--force-ca`, `--skip-start`, `--yes`

---

### Option B: Native Linux (Without Docker)

Requires **PHP >= 8.2** (`pdo_sqlite`, `openssl`, `mbstring`, `curl`, `xml`), **Node.js >= 20** (if frontend assets are not built yet), and optionally **Go** (falls back to Docker `golang` builder if installed) + **Nginx** (`--with-nginx`):

```bash
# Direct HTTP (ports 8000 / 8080 / 8082)
./new_setup_scripts/linux/setup-server-native.sh

# Full TLS + Agent mTLS via Nginx (ports 80 / 443 / 8443, requires sudo)
./new_setup_scripts/linux/setup-server-native.sh --with-nginx
```

**Common Flags (`setup-server-native.sh`):**
- `--with-nginx` (configures `/etc/nginx/sites-available/xephyguard.conf` or `/etc/nginx/conf.d/xephyguard.conf` using `ccc-deploy/nginx/dev-native.conf.example`)
- `--ip <IP>`, `--subnet <CIDR>`, `--tz <IANA_TZ>`, `--lab-name "<NAME>"`
- `--app-port <N>` (default `8000`), `--reverb-port <N>` (default `8080`), `--gateway-port <N>` (default `8082`)
- `--force-ca`, `--skip-start`

---

## 2. Windows Server Setup

**Double-click:** [`windows/setup-server.bat`](windows/setup-server.bat) (requests Administrator privileges via UAC automatically).

Or run from PowerShell:

```powershell
.\new_setup_scripts\windows\setup-server.ps1
```

What `setup-server.ps1` configures on any fresh Windows 10/11 / Windows Server PC:
1. Detects/installs PHP 8.3+ (downloads portable PHP into `tools\php\` if needed and activates `curl`, `mbstring`, `openssl`, `pdo_sqlite`, `sqlite3`, `zip`, `sockets`).
2. Installs PHP Composer dependencies (`server\vendor\`) and builds frontend assets via Node/npm (`server\public\build\`) if missing.
3. Downloads/extracts Nginx (default `1.26.2`) into `tools\nginx\` if absent.
4. Generates `server\.env` with random `APP_KEY`, Reverb keys, Screen Gateway secrets, SQLite config, detected LAN IP, and local Windows timezone mapped cleanly to an IANA ID.
5. Runs `artisan migrate --force`, initializes the Lab CA & server certificate (`artisan xephyguard:ca:init`), and runs `artisan xephyguard:bootstrap`.
6. Renders Nginx HTTP (`80`), HTTPS (`443`), and Agent mTLS (`8443`) config, validates `nginx -t`, adds Windows Defender Firewall inbound rules, and launches all services via `start_server\windows\start-server.ps1 -Restart`.

**Useful Parameters (`setup-server.ps1`):**
- `-ServerIp <IP>` (override auto-detected LAN IP)
- `-AppPort 8000`, `-ReverbPort 8080`, `-GatewayPort 8082`, `-HttpPort 80`, `-HttpsPort 443`, `-MtlsPort 8443`
- `-NginxDir <PATH>`, `-NginxVersion 1.26.2`, `-ForceDownload`
- `-EnableAutoStart` / `-DisableAutoStart` (adds/removes Windows Startup folder shortcut)
- `-SkipStart`

---

## 3. First Login & Student PC Deployment

### Step 1: Create Instructor Account
After the server starts, open the Dashboard URL printed at the end of setup and visit **`/setup`** (e.g. `https://<SERVER_IP>/setup` or `http://<SERVER_IP>:8000/setup`) to create the initial instructor account and view the Lab **Enrollment Key**.

### Step 2A: Deploy on Linux Student PCs

Either run the CCC-hosted one-liner directly on each Ubuntu/Debian student machine:

```bash
curl -fsSL http://<CCC_IP>/install/linux.sh | sudo bash -s -- --key <ENROLL_KEY>
```

Or copy/run [`linux/install-agent.sh`](linux/install-agent.sh) (completely self-contained, supports offline `.deb` via `--deb`):

```bash
sudo ./new_setup_scripts/linux/install-agent.sh --ccc <CCC_IP> --key <ENROLL_KEY>
# Optional SHA-256 verification and local package:
sudo ./new_setup_scripts/linux/install-agent.sh --ccc <CCC_IP> --key <ENROLL_KEY> --cafp <CA_SHA256> --deb ./xephyguard-agent.deb
```

### Step 2B: Mass Deploy on Windows Student PCs (WinRM)

From the Windows teacher PC (PowerShell as Administrator), [`windows/deploy-agents.ps1`](windows/deploy-agents.ps1) scans the `/24` LAN subnet (or an explicit `-IPList`) and deploys the MSI/Setup EXE in parallel over WinRM:

```powershell
.\new_setup_scripts\windows\deploy-agents.ps1
# Or specify exact student PCs / custom installer:
.\new_setup_scripts\windows\deploy-agents.ps1 -IPList @("192.168.10.21","192.168.10.22") -TeacherIP 192.168.10.1
```

Finally, approve newly enrolled PCs under **Enroll** in the CCC web dashboard.
