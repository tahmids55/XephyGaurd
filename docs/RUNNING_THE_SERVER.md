# Running the XephyGuard server (CCC) on Linux and Windows

The server (the "CCC") has two halves:

| Half | What it does | Runs on |
|---|---|---|
| **Application** | dashboard, agent API (enroll, events, policy), live updates (Reverb), sessions, alerts, reports, scoring | Linux **or** Windows |
| **Network layer** | DNS sinkhole (Unbound), firewall/gateway (nftables), web filter (mitmproxy) | **Linux only** (nftables and the transparent proxy need the Linux kernel) |

Without the network layer, XephyGuard still works: the agents on the lab PCs detect AI tools, local
LLMs, extensions, devices and paste/typing patterns and report them to the dashboard. Only blocking AI
**websites** at the network level is missing.

Pick one setup:

| # | Setup | Use it for | Network layer |
|---|---|---|---|
| **A** | Linux, Docker, `install.sh` | **Primary production lab stack (recommended)** | yes |
| **B** | Linux, native (no Docker) | development, demos, custom systemd hosts | optional |
| **C** | Windows + WSL2 (Ubuntu inside Windows) | a lab whose server is a Windows 11/Server host needing proxy filtering | proxy-only |
| **D** | Windows CCC service stack (`scripts/windows-ccc-service.ps1`) | **Supported Windows CCC deployment** (endpoint telemetry + dashboard, no Linux gateway) | no |

Ports used on the lab network, all setups:

| Port | What |
|---|---|
| TCP 80 | CA certificate (`/ca.crt`), agent installers, student notice |
| TCP 443 | dashboard |
| TCP 8443 | agents (TLS 1.3 with client certificates) |
| UDP 47474 | agent auto-discovery (optional; you can give agents the IP instead) |
| UDP/TCP 53, TCP 8080 | DNS and web filter (setup A, C only) |

---

## A. Linux with Docker (production)

Requirements: Ubuntu Server 22.04 or 24.04, 4 cores, 8 GB RAM, 100 GB disk. Two network cards for
gateway mode, or one for proxy-only mode.

```bash
sudo git clone <repo-url> /opt/xephyguard
cd /opt/xephyguard
sudo ./ccc-deploy/install.sh
```

The installer asks for the WAN and LAN cards, the CCC IP, the lab subnet and the lab name. It then
installs Docker and nftables, writes the secrets, creates the lab CA, starts everything and prints the
dashboard URL. Next:

1. Open `https://<CCC-IP>/` and finish the setup wizard (admin account, lab name, retention, notice).
2. Run `sudo xephyguard-ccc doctor`. Every check prints a plain-language fix if something is wrong.
3. Add PCs: the dashboard's **Add a PC** page shows the enrollment key and the CA fingerprint. See
   `SETUP_LINUX_PC.md` and `SETUP_WINDOWS_PC.md`.

Full details, lab-network wiring and day-to-day commands are in [`SETUP_CCC.md`](SETUP_CCC.md).

Single network card? Use proxy-only mode:
```bash
sudo ./ccc-deploy/install.sh --mode proxy_only
```
In this mode the lab router must send the lab's web traffic to the CCC (see `SETUP_CCC.md`).

---

## B. Linux, native (no Docker): development and demos

### 1. Install the tools (Ubuntu 24.04)
```bash
sudo apt install -y php8.3-cli php8.3-sqlite3 php8.3-mysql php8.3-mbstring php8.3-xml php8.3-curl \
     php8.3-zip php8.3-gd php8.3-intl composer nodejs npm nginx python3 python3-venv openssl
```
You need PHP 8.3 or newer and Node 20 or newer (`node -v`; use NodeSource if Ubuntu's is older).

### 2. Set up the Laravel app
```bash
cd xephyguard/server
cp .env.example .env
composer install
npm ci && npm run build
php artisan key:generate
touch database/database.sqlite          # SQLite is fine for one lab; MySQL 8.4 for production
```

Edit `server/.env` and set these values. Replace `192.168.10.2` with **this machine's LAN IP**:
```ini
APP_NAME=XephyGuard
APP_URL=https://192.168.10.2
BROADCAST_CONNECTION=reverb
REVERB_HOST=127.0.0.1
REVERB_PORT=8081
REVERB_SCHEME=http
REVERB_SERVER_HOST=127.0.0.1
REVERB_SERVER_PORT=8081
XG_CCC_IP=192.168.10.2
XG_LAB_SUBNET=192.168.10.0/24
XG_NETWORK_MODE=proxy_only
XG_ANALYSIS_URL=http://127.0.0.1:8090
XG_INTERNAL_URL=http://127.0.0.1:8000
XG_INTERNAL_TOKEN=change-me-to-a-long-random-string
XG_REQUIRE_MTLS=true
```
Change `REVERB_APP_KEY` and `REVERB_APP_SECRET` too (any random strings). Then:
```bash
php artisan migrate
php artisan xephyguard:ca:init --server-ip=192.168.10.2 --server-dns=localhost
php artisan xephyguard:bootstrap
php artisan xephyguard:doctor          # DNS / firewall / proxy checks fail without the network layer: expected
```

### 3. nginx front (TLS and agent mTLS)
Agents only talk to the server through nginx, which checks their client certificates.
```bash
sudo cp ../ccc-deploy/nginx/dev-native.conf.example /etc/nginx/conf.d/xephyguard.conf
sudo sed -i "s|@CA_DIR@|$PWD/storage/app/private/ca|g; s|@PUBLIC@|$PWD/public|g" /etc/nginx/conf.d/xephyguard.conf
sudo rm -f /etc/nginx/sites-enabled/default
sudo nginx -t && sudo systemctl reload nginx
```
nginx (user `www-data`) needs read access to `server.key` in that folder. The simplest fix:
`sudo chgrp www-data storage/app/private/ca/server.key && chmod 640 storage/app/private/ca/server.key`.
**Never** make `ca.key` readable by anyone else.

### 4. Start the services (4 terminals, or use the systemd units below)
```bash
php artisan serve --host=127.0.0.1 --port=8000      # web app
php artisan reverb:start --host=127.0.0.1 --port=8081
php artisan queue:work                              # scoring, reports
php artisan schedule:work                           # timetable, "machine silent" detection, cleanup
```
Analysis service (scores sessions and writes the explanation in reports):
```bash
cd ../analysis
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
export XG_DB_URL="sqlite:///$(realpath ../server/database/database.sqlite)"
export XG_INTERNAL_TOKEN=change-me-to-a-long-random-string   # same as in server/.env
python -m xg_analysis serve
```
For MySQL use `XG_DB_URL=mysql://user:password@127.0.0.1:3306/xephyguard`.

Open `https://192.168.10.2/` and the setup wizard starts.

### Optional: keep it running with systemd
Create `/etc/systemd/system/xg-web.service` and copy it for `xg-reverb`, `xg-worker` and
`xg-scheduler`, changing only `ExecStart`:
```ini
[Unit]
Description=XephyGuard web
After=network-online.target

[Service]
User=YOUR_USER
WorkingDirectory=/home/YOUR_USER/xephyguard/server
ExecStart=/usr/bin/php artisan serve --host=127.0.0.1 --port=8000
Restart=always

[Install]
WantedBy=multi-user.target
```
Then run `sudo systemctl daemon-reload && sudo systemctl enable --now xg-web xg-reverb xg-worker xg-scheduler`.
`php artisan serve` is fine for a pilot. For anything bigger use setup A, which runs php-fpm.

---

## C. Windows with WSL2 (a Windows PC as the lab server)

WSL2 runs a real Ubuntu inside Windows, so the Docker stack from setup A runs unchanged. You need
**Windows 11 22H2 or newer** (for "mirrored" networking, so lab PCs can reach the server on the
Windows PC's own IP), virtualization enabled in the BIOS, and 8 GB+ RAM.

### 1. Install WSL2 and Ubuntu (PowerShell as Administrator)
```powershell
wsl --install -d Ubuntu-24.04
```
Reboot if asked, open "Ubuntu 24.04" from the Start menu and create a Linux user.

### 2. Turn on mirrored networking and keep the VM running
Create `C:\Users\<you>\.wslconfig`:
```ini
[wsl2]
networkingMode=mirrored
memory=6GB
vmIdleTimeout=-1

[experimental]
hostAddressLoopback=true
```
In Ubuntu, make sure systemd is on: `/etc/wsl.conf` must contain
```ini
[boot]
systemd=true
```
Then in PowerShell: `wsl --shutdown`, and open Ubuntu again.

### 3. Open the Windows firewall (PowerShell as Administrator)
```powershell
# Allow inbound traffic into WSL (Hyper-V firewall)
Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
# And on the normal Windows firewall, only from the lab subnet:
New-NetFirewallRule -DisplayName "XephyGuard TCP" -Direction Inbound -Protocol TCP `
  -LocalPort 80,443,8443,8080,53 -RemoteAddress 192.168.10.0/24 -Action Allow
New-NetFirewallRule -DisplayName "XephyGuard UDP" -Direction Inbound -Protocol UDP `
  -LocalPort 53,47474 -RemoteAddress 192.168.10.0/24 -Action Allow
```
Give the Windows PC a **fixed IP** on the lab network (Settings → Network → Edit IP).

Check that nothing on Windows already uses these ports:
`netstat -ano | findstr ":80 :443 :8443 :53 "`. Common culprits are IIS, Skype, other web servers and
Internet Connection Sharing (port 53). Stop them first.

### 4. Install XephyGuard inside Ubuntu
Do **not** install Docker Desktop for this. If you have it, turn off its WSL integration for this
distro. `install.sh` installs Docker inside Ubuntu.
```bash
sudo git clone <repo-url> /opt/xephyguard      # keep it inside Linux, not under /mnt/c (much faster)
cd /opt/xephyguard
ip -br addr                                    # mirrored mode shows the Windows adapters, e.g. eth0
sudo ./ccc-deploy/install.sh --mode proxy_only --lan-if eth0 --wan-if eth0 \
     --ccc-ip <Windows-PC-IP> --subnet 192.168.10.0/24 --lab-name "Lab 1"
```
Open `https://<Windows-PC-IP>/` from another PC and finish the wizard. `sudo xephyguard-ccc doctor`
works as on Linux.

### 5. Start it automatically after a reboot
WSL starts only when something uses it. Create a scheduled task that runs at startup
(Task Scheduler → Create Task → "Run whether user is logged on or not"):
```text
Program:   C:\Windows\System32\wsl.exe
Arguments: -d Ubuntu-24.04 --exec /bin/sh -c "sleep infinity"
```
The containers restart by themselves (`restart: unless-stopped`).

### Limits of the WSL2 setup
* **Gateway mode is not supported.** Windows owns the network cards, so the lab cannot route through
  the Linux firewall. Use proxy-only mode (the lab router sends DNS and web traffic to the server), or
  run without the network layer and rely on the agents.
* Windows Update reboots stop the server until the scheduled task brings it back.
* For a long-term lab, a cheap dedicated Linux box (setup A) is more reliable.

---

## D. Windows, native (no Docker, no WSL): development and demos

This runs the application half only, with no DNS or web filtering.

### 1. Install the tools
| Tool | Where | Notes |
|---|---|---|
| PHP 8.3+ (x64, Thread Safe zip) | windows.php.net/download | unzip to `C:\php`, add to PATH |
| Composer | getcomposer.org/Composer-Setup.exe | |
| Node.js 20 LTS+ | nodejs.org | |
| nginx for Windows | nginx.org/en/download.html (stable) | unzip to `C:\nginx` |
| Python 3.11+ | python.org | tick "Add to PATH" |
| Git | git-scm.com | |

Create `C:\php\php.ini` from `php.ini-production` and enable these lines (remove the `;`):
```ini
extension_dir = "ext"
extension=curl
extension=fileinfo
extension=gd
extension=intl
extension=mbstring
extension=openssl
extension=pdo_mysql
extension=pdo_sqlite
extension=sqlite3
extension=zip
```
Check with `php -m`.

### 2. Set up the app (PowerShell, as a normal user)
```powershell
git clone <repo-url> C:\xephyguard
cd C:\xephyguard\server
copy .env.example .env
composer install
npm ci; npm run build
php artisan key:generate
New-Item database\database.sqlite -ItemType File
```
Edit `C:\xephyguard\server\.env` exactly as in **B.2** (use the Windows PC's LAN IP). Then:
```powershell
php artisan migrate
php artisan xephyguard:ca:init --server-ip=192.168.10.2 --server-dns=localhost
php artisan xephyguard:bootstrap
```
If `ca:init` fails with an OpenSSL configuration error, set this and run it again:
`$env:OPENSSL_CONF = "C:\php\extras\ssl\openssl.cnf"`.

Protect the CA key: only your account and Administrators should read
`C:\xephyguard\server\storage\app\private\ca\ca.key`. Folder → Properties → Security → Advanced →
disable inheritance → remove "Users".

### 3. nginx
```powershell
copy C:\xephyguard\ccc-deploy\nginx\dev-native.conf.example C:\nginx\conf\xephyguard.conf
```
In `C:\nginx\conf\xephyguard.conf`, replace `@CA_DIR@` with
`C:/xephyguard/server/storage/app/private/ca` and `@PUBLIC@` with `C:/xephyguard/server/public`. Use
**forward slashes**.
In `C:\nginx\conf\nginx.conf`, delete the example `server { listen 80; ... }` block inside `http { }`
and add `include xephyguard.conf;` in its place. Then:
```powershell
cd C:\nginx
.\nginx.exe -t        # must say "syntax is ok"
start nginx           # stop later with: .\nginx.exe -s stop
```

### 4. Start the services (5 PowerShell windows)
```powershell
cd C:\xephyguard\server; php artisan serve --host=127.0.0.1 --port=8000
cd C:\xephyguard\server; php artisan reverb:start --host=127.0.0.1 --port=8081
cd C:\xephyguard\server; php artisan queue:work
cd C:\xephyguard\server; php artisan schedule:work
```
```powershell
cd C:\xephyguard\analysis
python -m venv .venv; .\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
$env:XG_DB_URL = "sqlite:///C:/xephyguard/server/database/database.sqlite"
$env:XG_INTERNAL_TOKEN = "change-me-to-a-long-random-string"   # same as in .env
python -m xg_analysis serve
```

### 5. Firewall
```powershell
New-NetFirewallRule -DisplayName "XephyGuard" -Direction Inbound -Protocol TCP `
  -LocalPort 80,443,8443 -RemoteAddress 192.168.10.0/24 -Action Allow
```
Windows asks to allow `nginx.exe` the first time. Allow it on **Private** networks only.
Ports 8000, 8081 and 8090 stay on `127.0.0.1` and must not be opened.

Open `https://<Windows-PC-IP>/` and finish the wizard.

To start and supervise everything automatically in production on Windows, use the built-in service wrapper `scripts/windows-ccc-service.ps1` (run PowerShell as Administrator). It registers `xg-ccc-web`, `xg-ccc-reverb`, `xg-ccc-worker`, `xg-ccc-scheduler`, `xg-ccc-analysis`, and `xg-ccc-nginx` via NSSM (when installed) with `sc.exe failure` auto-restart recovery, or via SYSTEM Scheduled Tasks with a 1-minute watchdog restart interval:

```powershell
# Register all 6 CCC background services with automatic restart on failure
.\scripts\windows-ccc-service.ps1 install -RootDir C:\xephyguard -PhpExe C:\php\php.exe -NginxDir C:\nginx

# Start, inspect status, and run an end-to-end /healthz readiness check
.\scripts\windows-ccc-service.ps1 start
.\scripts\windows-ccc-service.ps1 status
.\scripts\windows-ccc-service.ps1 health
```

---

## Observability, Backups & SBOM (All Setups)

### 1. Health & Prometheus Metrics
* **`GET /healthz`**: Returns JSON readiness status (`200 OK` when healthy, `503 Service Unavailable` when degraded):
  ```json
  {
    "status": "ok",
    "db": true,
    "ca_initialized": true,
    "active_signature_version": "2026.11.01",
    "pcs_online": 28,
    "pcs_isolated": 0,
    "queue_depth": 0,
    "timestamp": "2026-11-07T08:00:00+00:00"
  }
  ```
* **`GET /metrics`**: Exports Prometheus text metrics (`xephyguard_pcs_total`, `xephyguard_pcs_online`, `xephyguard_pcs_isolated`, `xephyguard_pcs_silent`, `xephyguard_events_total`, `xephyguard_alerts_open_total` by severity, `xephyguard_agents_by_version`). Accessible from loopback (`127.0.0.1`), authenticated staff sessions, or via `Authorization: Bearer <XG_METRICS_TOKEN>`.

### 2. Automated Backups & Verified Restore
Create a signed/checksummed backup archive containing a consistent database dump, CA certificates and keys (`ca.crt`, `signing.crt`, `interception-ca.crt`), `.env` metadata, and a `manifest.json` with SHA-256 digests of every file:
```bash
php artisan xephyguard:backup --output=/var/backups/xephyguard-backup.tar.gz
```
Verify archive integrity (and dry-run restore verification) without modifying the live server:
```bash
php artisan xephyguard:restore /var/backups/xephyguard-backup.tar.gz --verify-only
```
Restore from a verified archive:
```bash
php artisan xephyguard:restore /var/backups/xephyguard-backup.tar.gz
```

### 3. CycloneDX 1.5 SBOM Generation
Generate or refresh the CycloneDX 1.5 JSON Software Bill of Materials (`dist/sbom.cdx.json`) covering C++ (`agent/cmake/Dependencies.cmake`), PHP (`server/composer.lock`), and Python (`analysis/requirements.txt`, `proxy/requirements.txt`) dependencies:
```bash
python3 scripts/generate-sbom.py --output dist/sbom.cdx.json
```

---

## Connecting lab PCs (all setups)

1. The dashboard's **Add a PC** page shows the enrollment key, the server IP and the CA fingerprint.
2. Install the agent on the PC (`.deb` for Linux, `xephyguard-agent-setup.exe` for Windows), then run:
   ```text
   xephyguard-agent enroll --ccc <server-IP> --key <enrollment-key> --ca-fingerprint <fingerprint>
   ```
   The agent downloads `http://<server-IP>/ca.crt`, checks the fingerprint, and enrolls on port 8443.
3. The PC appears as **Pending** on the **Add a PC** page. Click **Approve**; it turns green within seconds.

Details: [`SETUP_LINUX_PC.md`](SETUP_LINUX_PC.md), [`SETUP_WINDOWS_PC.md`](SETUP_WINDOWS_PC.md).

---

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| Browser certificate warning | expected until you trust the lab CA: download `http://<IP>/ca.crt`, compare the fingerprint, install it as a trusted root on admin machines |
| Dashboard works but does not update live | Reverb is not running, or `REVERB_*` in `.env` does not match port 8081; restart `reverb:start` and reload the page |
| Agent: `cannot download the CA certificate` | port 80 blocked (firewall) or nginx not running |
| Agent: `403 client certificate missing` | the agent is talking to Laravel directly; it must use nginx on 8443. Check `XG_REQUIRE_MTLS=true` and that nginx forwards `X-SSL-Client-*` |
| PC stays "Pending" | click **Approve** on the **Add a PC** page; check the enrollment key has not been rotated |
| Report has no score | analysis service not running, or `XG_INTERNAL_TOKEN` differs between `.env` and the analysis service |
| PC shows "Machine silent" | the scheduler (`schedule:work`) is running but the agent stopped sending heartbeats: check the PC and its network |
| WSL2: lab PCs cannot connect | mirrored networking not on (`wsl --shutdown` after editing `.wslconfig`), Hyper-V firewall rule missing, or the Windows PC's IP changed |
| `php artisan ...` fails with `could not find driver` | enable `pdo_sqlite` (or `pdo_mysql`) in `php.ini` |
