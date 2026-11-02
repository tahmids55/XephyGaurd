# XephyGuard: complete lab setup guide

This guide takes a computer lab from nothing to a working XephyGuard installation: the central
server (the **CCC**), the agent installers, the lab PCs, the dashboard and a final acceptance test.
Every step that differs between operating systems has a **Linux** and a **Windows** section.

> **Status of the instructions**
> * Tested end to end: Linux server with Docker, Linux server without Docker, Linux lab PCs (`.deb`).
> * Written but **not yet run on real Windows**: Windows server (WSL2 or native), Windows lab PCs.
>   Before you rely on Windows lab PCs, run `docs/WINDOWS_TEST_CHECKLIST.md` on one machine.
> * Not yet run on a real two-network-card gateway with lab PCs behind it: `install.sh` gateway mode.
>   Plan a test day before the first real class (Part 6).

---

## Contents

0. [How XephyGuard fits into a lab](#0-how-xephyguard-fits-into-a-lab)
1. [Plan the lab](#1-plan-the-lab)
2. [Set up the server (CCC)](#2-set-up-the-server-ccc)
   * [2-L. Linux server (recommended)](#2-l-linux-server-recommended)
   * [2-W. Windows server](#2-w-windows-server)
3. [First-run wizard and dashboard basics](#3-first-run-wizard-and-dashboard-basics)
4. [Build and publish the agent installers](#4-build-and-publish-the-agent-installers)
   * [4-L. Linux agent (.deb)](#4-l-linux-agent-deb)
   * [4-W. Windows agent (.exe)](#4-w-windows-agent-exe)
5. [Install the agent on the lab PCs](#5-install-the-agent-on-the-lab-pcs)
   * [5-L. Linux lab PCs](#5-l-linux-lab-pcs)
   * [5-W. Windows lab PCs](#5-w-windows-lab-pcs)
6. [Acceptance test (before the first class)](#6-acceptance-test-before-the-first-class)
7. [Running classes and tests](#7-running-classes-and-tests)
8. [Maintenance](#8-maintenance)
9. [Troubleshooting](#9-troubleshooting)
10. [Reference: ports, files, commands](#10-reference-ports-files-commands)

---

## 0. How XephyGuard fits into a lab

```
                 Internet / campus network
                           │
                    (WAN network card)
                ┌──────────┴───────────┐
                │      CCC server      │  dashboard, agent API, database, reports
                │  + DNS sinkhole      │  (Unbound)
                │  + firewall/gateway  │  (nftables)        ← "network layer", Linux only
                │  + web filter        │  (mitmproxy)
                └──────────┬───────────┘
                    (LAN network card)   e.g. 192.168.10.2
                           │
                     ── lab switch ──
               │        │        │        │
             PC 01    PC 02    PC 03 ...  PC n      each runs the XephyGuard agent
```

There are two kinds of protection:

| Layer | What it does | Needs |
|---|---|---|
| **Agent** (on every lab PC) | detects AI apps, local LLM servers, AI browser extensions, local model files, VMs, hotspots/VPNs, USB devices, unusual paste/typing patterns; can close programs; shows a "Monitored" indicator | Linux or Windows PCs |
| **Network layer** (on the CCC) | blocks AI websites and tricks to bypass DNS (DoH, DoT, QUIC), removes Google's AI Overview, isolates single PCs | a **Linux** CCC that the lab's traffic passes through |

The agents work on their own. The network layer adds website blocking, which students cannot
switch off on their PC.

**Modes** (chosen per session from the dashboard):
* **Always-on** (no session running): only AI tools, extensions, devices and network changes are logged.
* **Practice**: AI websites allowed or blocked as the profile says; typing/paste statistics recorded.
* **Test**: AI blocked at every layer, all detections raise alerts.

**Privacy:** keystrokes and clipboard text are reduced to numbers on the PC and never leave it. No
screenshots, no file contents, no passwords. See `docs/ETHICS.md`.

---

## 1. Plan the lab

### 1.1 Before you install anything
- [ ] Ethics/privacy approval from your institution. `docs/ETHICS.md` contains a ready statement and
      a consent template.
- [ ] Decide who gets dashboard accounts: **admin** (everything), **instructor** (sessions,
      reports, timetable, roster), **assistant** (live lab, alerts, adding PCs).
- [ ] Decide the network mode (1.3).
- [ ] Get admin access to the lab PCs and the lab's network equipment (router/DHCP).

### 1.2 Hardware for the CCC
| | Minimum | Recommended |
|---|---|---|
| CPU / RAM | 4 cores / 8 GB | 8 cores / 16 GB (more than 60 PCs) |
| Disk | 100 GB SSD | 250 GB SSD |
| Network | 1 card (proxy-only mode) | **2 cards** (gateway mode) |
| OS | Ubuntu Server 22.04 / 24.04 LTS | same |

A Windows PC can be the CCC (2-W), but gateway mode, the strongest option, needs Linux.

### 1.3 Choose the network mode
| Mode | How lab traffic reaches the CCC | Strength | CCC OS |
|---|---|---|---|
| **Gateway** (recommended) | the CCC *is* the lab's router: PCs get it as default gateway and DNS | students cannot bypass it with proxy settings | Linux, 2 network cards |
| **Proxy-only** | the existing lab router forwards web (TCP 80/443) and DNS to the CCC | weaker: depends on the router's rules | Linux (or Windows + WSL2, untested) |
| **Agent-only** | no network layer; only the agents | no website blocking | Linux or Windows |

### 1.4 IP plan (example used throughout this guide)
| Item | Value |
|---|---|
| Lab subnet | `192.168.10.0/24` |
| CCC (LAN side) | `192.168.10.2` (fixed) |
| Lab PCs | `192.168.10.21` – `192.168.10.200`, one **DHCP reservation per PC** (a PC must keep its IP) |
| Gateway + DNS handed to PCs | gateway mode: `192.168.10.2`; proxy-only mode: the router, which forwards DNS to `192.168.10.2` |

Replace these values with your own everywhere below.

---

## 2. Set up the server (CCC)

Pick **one** of 2-L or 2-W.

---

### 2-L. Linux server (recommended)

#### 2-L.1 Install Ubuntu Server
1. Install Ubuntu Server 22.04 or 24.04 LTS (minimal install, OpenSSH server on).
2. Note the names of the network cards: `ip -br link` (for example `enp1s0` and `enp2s0`).
   Plug the **WAN** card into the campus network/Internet and the **LAN** card into the lab switch.
3. Give the LAN card the fixed CCC address. Create `/etc/netplan/60-xephyguard.yaml`:
   ```yaml
   network:
     version: 2
     ethernets:
       enp2s0:                          # LAN card
         addresses: [192.168.10.2/24]
         dhcp4: false
   ```
   Then `sudo chmod 600 /etc/netplan/60-xephyguard.yaml && sudo netplan apply`.
4. Set the correct time zone, which the timetable depends on: `sudo timedatectl set-timezone Asia/Dhaka`
   (use yours).

#### 2-L.2 Run the installer
```bash
sudo apt update && sudo apt install -y git
sudo git clone https://github.com/tahmids55/XephyGaurd.git /opt/xephyguard
cd /opt/xephyguard
sudo ./ccc-deploy/install.sh
```
The installer asks for the WAN card, the network mode, the LAN card, the CCC IP, the subnet and the
lab name. You can also pass everything at once:
```bash
# gateway mode (2 cards)
sudo ./ccc-deploy/install.sh --wan-if enp1s0 --lan-if enp2s0 --ccc-ip 192.168.10.2 \
     --subnet 192.168.10.0/24 --lab-name "Lab 3" --mode gateway --yes

# proxy-only mode (1 card)
sudo ./ccc-deploy/install.sh --wan-if enp1s0 --lan-if enp1s0 --ccc-ip 192.168.10.2 \
     --subnet 192.168.10.0/24 --lab-name "Lab 3" --mode proxy_only --yes
```
What it does:
* installs Docker, nftables and chrony;
* turns on IP forwarding and frees port 53;
* writes the secrets to `ccc-deploy/.env` (mode 0600);
* creates the lab certificate authority (CA);
* writes the firewall rules (gateway mode) and starts all containers.

At the end it prints the **dashboard URL** and the **CA fingerprint**. Write the fingerprint down;
you need it for the lab PCs.

The installer is safe to re-run; it keeps existing secrets, the CA and data.

#### 2-L.3 Make lab traffic go through the CCC

**Gateway mode.** Lab PCs must get `192.168.10.2` as **default gateway** and **DNS server**
(DHCP options 3 and 6), and no other way out (no second router on the lab switch, Wi-Fi off or
monitored).

* *If an existing DHCP server serves the lab*, change its router and DNS options to `192.168.10.2`
  for the lab subnet and add a reservation for every PC.
* *If the CCC should be the lab's DHCP server*, this **example** uses dnsmasq in DHCP-only mode, so
  it does not clash with XephyGuard's DNS on port 53. It is not part of `install.sh`, so test it first:
  ```bash
  sudo apt install -y dnsmasq
  sudo tee /etc/dnsmasq.d/xephyguard-dhcp.conf >/dev/null <<'EOF'
  port=0                                   # DHCP only; DNS is XephyGuard's Unbound
  interface=enp2s0
  bind-interfaces
  dhcp-range=192.168.10.21,192.168.10.200,255.255.255.0,12h
  dhcp-option=option:router,192.168.10.2
  dhcp-option=option:dns-server,192.168.10.2
  # one line per PC (MAC address, fixed IP, name)
  dhcp-host=aa:bb:cc:dd:ee:01,192.168.10.21,lab-pc-01
  EOF
  sudo systemctl restart dnsmasq
  ```

**Proxy-only mode.** On the lab router:
* forward TCP 80 and 443 from the lab subnet to `192.168.10.2:8080`;
* hand out `192.168.10.2` as the DNS server;
* block UDP 443 (QUIC) and TCP/UDP 853 (DNS-over-TLS) from the lab subnet.

#### 2-L.4 Check the server
```bash
sudo xephyguard-ccc doctor
```
Every line is either OK or tells you in plain words what is wrong and how to fix it. Checks include
IP forwarding, firewall, DNS, proxy, time sync, disk, containers, CA expiry, signatures, the analysis
service and silent PCs.

Continue with **[Part 3](#3-first-run-wizard-and-dashboard-basics)**.

#### 2-L.5 (Alternative) Linux server without Docker
This is for development or a demo without the network layer. Summary (full commands in
`docs/RUNNING_THE_SERVER.md`, setup B):
```bash
sudo apt install -y php8.3-cli php8.3-sqlite3 php8.3-mysql php8.3-mbstring php8.3-xml php8.3-curl \
     php8.3-zip php8.3-gd php8.3-intl composer nodejs npm nginx python3 python3-venv openssl
cd /opt/xephyguard/server
cp .env.example .env && composer install && npm ci && npm run build && php artisan key:generate
touch database/database.sqlite
# edit .env: APP_URL, BROADCAST_CONNECTION=reverb, REVERB_* on port 8081, XG_CCC_IP, XG_LAB_SUBNET,
#            XG_NETWORK_MODE=proxy_only, XG_ANALYSIS_URL=http://127.0.0.1:8090, XG_INTERNAL_TOKEN=<random>
php artisan migrate
php artisan xephyguard:ca:init --server-ip=192.168.10.2 --server-dns=localhost
php artisan xephyguard:bootstrap
# nginx front: ccc-deploy/nginx/dev-native.conf.example → /etc/nginx/conf.d/xephyguard.conf
# then run: php artisan serve / reverb:start / queue:work / schedule:work, and the analysis service
```

---

### 2-W. Windows server

Windows cannot run the network layer itself (nftables and the transparent proxy need the Linux
kernel). There are two options:

| Option | What you get | Use for |
|---|---|---|
| **W1. WSL2** (Ubuntu inside Windows) | everything from 2-L in **proxy-only** mode | a lab whose only server is a Windows PC |
| **W2. Native** (PHP, nginx and Python for Windows) | dashboard + agents, **no website blocking** | demos, development, agent-only labs |

#### W1. Windows server with WSL2

**Requirements:**
* Windows 11 22H2 or newer (for mirrored networking), Pro or Education;
* virtualization enabled in the BIOS;
* 16 GB RAM recommended;
* a **fixed IP** for the Windows PC on the lab network (Settings → Network → Edit IP), e.g. `192.168.10.2`.

1. **Install WSL2 and Ubuntu** (PowerShell as Administrator):
   ```powershell
   wsl --install -d Ubuntu-24.04
   ```
   Reboot, open "Ubuntu 24.04" from the Start menu and create a Linux user.

2. **Mirrored networking**, so lab PCs reach the server on the Windows PC's IP. Create
   `C:\Users\<you>\.wslconfig`:
   ```ini
   [wsl2]
   networkingMode=mirrored
   memory=8GB
   vmIdleTimeout=-1

   [experimental]
   hostAddressLoopback=true
   ```
   In Ubuntu, `/etc/wsl.conf` must contain:
   ```ini
   [boot]
   systemd=true
   ```
   Then run `wsl --shutdown` in PowerShell and open Ubuntu again.

3. **Windows firewall** (PowerShell as Administrator):
   ```powershell
   Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
   New-NetFirewallRule -DisplayName "XephyGuard TCP" -Direction Inbound -Protocol TCP `
     -LocalPort 80,443,8443,8080,53 -RemoteAddress 192.168.10.0/24 -Action Allow
   New-NetFirewallRule -DisplayName "XephyGuard UDP" -Direction Inbound -Protocol UDP `
     -LocalPort 53,47474 -RemoteAddress 192.168.10.0/24 -Action Allow
   ```
   Make sure nothing on Windows already uses these ports (IIS, Internet Connection Sharing, other web
   servers): `netstat -ano | findstr ":80 :443 :8443 :53 "`.

4. **Install XephyGuard inside Ubuntu.** Don't use Docker Desktop for this; if it is installed,
   turn off its WSL integration for this distro.
   ```bash
   sudo git clone https://github.com/tahmids55/XephyGaurd.git /opt/xephyguard   # inside Linux, not /mnt/c
   cd /opt/xephyguard
   ip -br addr                     # mirrored mode shows the Windows adapters, e.g. eth0
   sudo ./ccc-deploy/install.sh --mode proxy_only --wan-if eth0 --lan-if eth0 \
        --ccc-ip 192.168.10.2 --subnet 192.168.10.0/24 --lab-name "Lab 3"
   sudo xephyguard-ccc doctor
   ```

5. **Lab router**: set up proxy-only forwarding as in 2-L.3.

6. **Start automatically after reboots.** Task Scheduler → Create Task:
   * "Run whether user is logged on or not";
   * trigger "At startup";
   * action: program `C:\Windows\System32\wsl.exe`, arguments `-d Ubuntu-24.04 --exec /bin/sh -c "sleep infinity"`.

   The containers restart by themselves.

7. **Windows Update**: set active hours so the server does not reboot during lab time.

#### W2. Windows server, native (no website blocking)

1. **Install the tools:**
   * PHP 8.3+ x64 Thread Safe (windows.php.net), unzipped to `C:\php` and added to PATH;
   * Composer;
   * Node.js 20 LTS;
   * nginx for Windows (nginx.org), unzipped to `C:\nginx`;
   * Python 3.11+ ("Add to PATH");
   * Git.
2. **Configure PHP.** Copy `C:\php\php.ini-production` to `C:\php\php.ini` and enable
   `extension_dir = "ext"` and the extensions `curl fileinfo gd intl mbstring openssl pdo_mysql
   pdo_sqlite sqlite3 zip`. Check with `php -m`.
3. **Set up the app** (PowerShell):
   ```powershell
   git clone https://github.com/tahmids55/XephyGaurd.git C:\xephyguard
   cd C:\xephyguard\server
   copy .env.example .env
   composer install
   npm ci; npm run build
   php artisan key:generate
   New-Item database\database.sqlite -ItemType File
   ```
   Edit `.env` and set:
   ```ini
   APP_NAME=XephyGuard
   APP_URL=https://192.168.10.2
   BROADCAST_CONNECTION=reverb
   REVERB_HOST=127.0.0.1
   REVERB_PORT=8081
   REVERB_SCHEME=http
   REVERB_SERVER_HOST=127.0.0.1
   REVERB_SERVER_PORT=8081
   REVERB_APP_KEY=<random>
   REVERB_APP_SECRET=<random>
   XG_CCC_IP=192.168.10.2
   XG_LAB_SUBNET=192.168.10.0/24
   XG_NETWORK_MODE=proxy_only
   XG_ANALYSIS_URL=http://127.0.0.1:8090
   XG_INTERNAL_URL=http://127.0.0.1:8000
   XG_INTERNAL_TOKEN=<long random string>
   XG_REQUIRE_MTLS=true
   ```
   Then:
   ```powershell
   php artisan migrate
   php artisan xephyguard:ca:init --server-ip=192.168.10.2 --server-dns=localhost
   php artisan xephyguard:bootstrap
   ```
   If `ca:init` reports an OpenSSL config error, run
   `$env:OPENSSL_CONF = "C:\php\extras\ssl\openssl.cnf"` and try again.
   Restrict `storage\app\private\ca\ca.key` to your account and Administrators (Properties →
   Security → Advanced → disable inheritance → remove "Users").
4. **Configure nginx:**
   * copy `C:\xephyguard\ccc-deploy\nginx\dev-native.conf.example` to `C:\nginx\conf\xephyguard.conf`;
   * replace `@CA_DIR@` with `C:/xephyguard/server/storage/app/private/ca` and `@PUBLIC@` with
     `C:/xephyguard/server/public` (use forward slashes);
   * in `C:\nginx\conf\nginx.conf`, replace the example `server { listen 80; … }` block with
     `include xephyguard.conf;`;
   * run `cd C:\nginx; .\nginx.exe -t; start nginx`.
5. **Start the services** in five PowerShell windows, or as services with NSSM:
   ```powershell
   cd C:\xephyguard\server; php artisan serve --host=127.0.0.1 --port=8000
   cd C:\xephyguard\server; php artisan reverb:start --host=127.0.0.1 --port=8081
   cd C:\xephyguard\server; php artisan queue:work
   cd C:\xephyguard\server; php artisan schedule:work
   cd C:\xephyguard\analysis; python -m venv .venv; .\.venv\Scripts\Activate.ps1; pip install -r requirements.txt
   $env:XG_DB_URL="sqlite:///C:/xephyguard/server/database/database.sqlite"; $env:XG_INTERNAL_TOKEN="<same as .env>"; python -m xg_analysis serve
   ```
6. **Open the firewall** for the lab subnet only:
   ```powershell
   New-NetFirewallRule -DisplayName "XephyGuard" -Direction Inbound -Protocol TCP `
     -LocalPort 80,443,8443 -RemoteAddress 192.168.10.0/24 -Action Allow
   ```
   Ports 8000, 8081 and 8090 stay on `127.0.0.1` and must not be opened.

More detail: `docs/RUNNING_THE_SERVER.md`, setups C and D.

---

## 3. First-run wizard and dashboard basics

1. On an admin computer, open `http://192.168.10.2/ca.crt`. Compare its SHA-256 fingerprint with the
   one the installer printed, then install it as a trusted root certificate:
   * **Windows:** double-click → Install Certificate → Local Machine → "Trusted Root Certification Authorities";
   * **Linux:** `sudo cp ca.crt /usr/local/share/ca-certificates/xephyguard.crt && sudo update-ca-certificates`.

   Without this, the browser shows a certificate warning, which you can accept once.
2. Open `https://192.168.10.2/`. The **setup wizard** asks for:
   * the admin account;
   * the lab name;
   * the network mode (gateway / proxy only);
   * the lab subnet and the CCC's fixed IP;
   * how long to keep raw events (default ~1 semester);
   * the notice text shown to students.
3. **Users**: create accounts for instructors and lab assistants and choose their roles.
4. **Settings**: check retention, the notice text and the alert thresholds.
5. **Timetable & profiles**: the built-in profiles are *Practice* and *Test: AI blocked*. Add
   timetable entries (day, time, course, profile) if sessions should start and stop by themselves.
6. **Roster** (optional): import students. This is also where research consent is tracked.
7. **Blocklists & signatures**: the default AI site lists and detection rules are already published.
   Add your own sites here, then click **Publish**.

---

## 4. Build and publish the agent installers

The lab PCs download installers from the CCC (`http://<CCC>/downloads/...`). Build them once per
release and copy them to the CCC's downloads folder:

| CCC type | Downloads folder |
|---|---|
| Linux / WSL2 (Docker) | `/var/lib/xephyguard/storage/public/downloads/` |
| Native (no Docker) | `server/storage/app/public/downloads/` |

The **Add a PC** page lists every file in that folder.

---

### 4-L. Linux agent (.deb)

Build on Ubuntu 22.04/24.04. This can be the CCC itself or any other Linux machine with the same
architecture as the lab PCs:
```bash
sudo apt install -y build-essential cmake git libssl-dev libsqlite3-dev libx11-dev libxfixes-dev \
     python3-yaml dpkg-dev
cd /opt/xephyguard
sudo scripts/release.sh 0.1.0 --publish-to /var/lib/xephyguard/storage/public/downloads
```
This builds `xephyguard-agent_0.1.0_amd64.deb` plus the alias `xephyguard-agent_latest_amd64.deb`,
which the one-line installer downloads, and copies them to the downloads folder. Everything is also
kept in `dist/0.1.0/`.

Build it on the oldest Ubuntu/Debian release used in the lab, so the package's library
requirements are satisfied everywhere.

To build by hand instead:
```bash
cmake -S agent -B agent/build-release -DCMAKE_BUILD_TYPE=Release -DXG_BUILD_TESTS=OFF
cmake --build agent/build-release -j"$(nproc)"
(cd agent/build-release && cpack -G DEB)
```

---

### 4-W. Windows agent (.exe)

The Windows agent must be compiled with Visual Studio on a Windows machine. This build PC is any
Windows 10/11 machine, not a lab PC.

1. **Install the tools:**
   * Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload;
   * CMake;
   * Git;
   * Python 3 with `pip install pyyaml`;
   * vcpkg:
     ```powershell
     git clone https://github.com/microsoft/vcpkg C:\vcpkg
     C:\vcpkg\bootstrap-vcpkg.bat
     $env:VCPKG_ROOT = "C:\vcpkg"
     ```
2. **Build** (Developer PowerShell for VS 2022):
   ```powershell
   git clone https://github.com/tahmids55/XephyGaurd.git C:\xg-build
   cd C:\xg-build
   C:\vcpkg\vcpkg install openssl sqlite3 --triplet x64-windows-static
   cmake -S agent -B agent/build-windows -G "Visual Studio 17 2022" -A x64 `
     -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake `
     -DVCPKG_TARGET_TRIPLET=x64-windows-static -DXG_BUILD_TESTS=OFF
   cmake --build agent/build-windows --config Release
   ```
   The result is `agent\build-windows\Release\xephyguard-agent.exe` and `xephyguard-helper.exe`.
3. **Package**, using either of these:
   * **On Linux (easiest):** copy the `agent\build-windows\Release` folder to the same path in the
     repository on the Linux build machine, run `sudo apt install -y nsis`, and run
     `scripts/release.sh` as in 4-L. It then also builds `XephyGuard-Setup-0.1.0.exe` and
     `XephyGuard-Setup-latest.exe` and publishes them.
   * **On Windows:** install NSIS, then run:
     ```powershell
     python scripts\compile_signatures.py --out installers\windows\signatures.json
     cd installers\windows
     & "C:\Program Files (x86)\NSIS\makensis.exe" -DVERSION=0.1.0 -DBUILD_DIR=..\..\agent\build-windows\Release xephyguard.nsi
     ```
     Copy `XephyGuard-Setup-0.1.0.exe` into the CCC's downloads folder.
4. **Code signing** (recommended for large deployments): sign both `.exe` files and the installer
   with your institution's certificate (`signtool sign /fd sha256 ...`). Otherwise SmartScreen may warn.

---

## 5. Install the agent on the lab PCs

For every PC, the dashboard's **Add a PC** page shows:
* the **enrollment key** (`XG-XXXXXX-XXXXXX-XXXXXX`);
* the CCC address;
* the **CA fingerprint**;
* ready-made commands with these values filled in.

After installing, each PC appears there as **Pending**. Click **Approve** and give it a name (e.g.
`A3`).

**For mass rollouts:** paste the lab's MAC addresses on the **Add a PC** page and tick
"Auto-approve PCs whose MAC is on this list" first.

---

### 5-L. Linux lab PCs

Supported: Ubuntu 22.04/24.04 and Debian 12, amd64.

#### 5-L.1 One PC (one-liner)
On the PC, as an admin user:
```bash
curl -fsSL http://192.168.10.2/install/linux.sh | sudo bash -s -- --key XG-XXXXXX-XXXXXX-XXXXXX
```
The script:
1. downloads the CA and checks its fingerprint (it aborts on a mismatch);
2. installs the `.deb`;
3. enrolls and waits up to 10 minutes for your approval;
4. shows the status.

#### 5-L.2 Manual
```bash
sudo apt install ./xephyguard-agent_0.1.0_amd64.deb
sudo xephyguard-agent enroll --key XG-... --ccc 192.168.10.2 --ca-fingerprint <sha256>
```

#### 5-L.3 Many PCs (Ansible)
On an admin machine with Ansible:
```bash
cp installers/ansible/inventory.example.ini inventory.ini     # list your lab PCs
ansible-playbook -i inventory.ini installers/ansible/site.yml \
  -e xephyguard_enroll_key=XG-... -e xephyguard_ca_fingerprint=<sha256>
```
The role installs the agent, enrolls it and switches the login screen to X11 (`WaylandEnable=false`).

#### 5-L.4 Prepare the PCs
* **Use an X11 session.** On Wayland the agent cannot see window titles or the clipboard; typing
  statistics still work. The dashboard shows what each PC can see in the PC drawer under "Can see".
* Students use **standard accounts without sudo**.
* Don't install Docker/Podman or `ollama` for student accounts. If they are needed, they are
  detected (`vm.*` rules).
* VS Code with a root-owned extensions directory:
  `code --extensions-dir /opt/xephyguard/vscode-extensions`.
* BIOS password and a fixed boot order, so nobody can boot another OS from USB.
* Wi-Fi off on wired lab PCs, or leave it on and rely on the hotspot/adapter alerts.

#### 5-L.5 What gets installed
| Path | Purpose |
|---|---|
| `/usr/lib/xephyguard/xephyguard-agent` | system service `xephyguard-agent.service` (restarts automatically, watchdog) |
| `/usr/lib/xephyguard/xephyguard-helper` | per-user helper: tray indicator, window and clipboard statistics |
| `/etc/xephyguard/agent.toml` (root, 0600) | configuration; editing it raises a tamper alert |
| `/var/lib/xephyguard/queue.db` | offline buffer (events are kept if the network is down) |
| `/var/log/xephyguard/agent.log` | log |
| system CA store, Chromium/Firefox | lab CA trust |
| `/etc/opt/chrome/…`, `/etc/opt/edge/…`, `/etc/firefox/policies/` | browser policies: AI features and DNS-over-HTTPS off |

#### 5-L.6 Check a PC
```bash
sudo xephyguard-agent status          # enrollment, CCC connection, queue
systemctl status xephyguard-agent
sudo xephyguard-agent scan            # one-off local scan: prints what would be flagged
```

---

### 5-W. Windows lab PCs

Supported: Windows 10 and 11, x64. **Run `docs/WINDOWS_TEST_CHECKLIST.md` on one PC first.**

#### 5-W.1 One PC
1. On the PC, open `http://192.168.10.2/` → **Add a PC** link, or download
   `http://192.168.10.2/downloads/XephyGuard-Setup-latest.exe`.
2. Install it **as administrator**. The silent form (copy it from the Add a PC page) also enrolls:
   ```
   XephyGuard-Setup-latest.exe /S /KEY=XG-XXXXXX-XXXXXX-XXXXXX /CCC=192.168.10.2 /CAFP=<sha256>
   ```
3. If you installed interactively (without `/KEY`), enroll from an administrator PowerShell:
   ```powershell
   & "C:\Program Files\XephyGuard\xephyguard-agent.exe" enroll --key XG-... --ccc 192.168.10.2 --ca-fingerprint <sha256>
   Restart-Service XephyGuardAgent
   ```
4. Approve the PC on the dashboard.

#### 5-W.2 Many PCs
| Method | How |
|---|---|
| **Group Policy** | Put the installer on a share readable by *Domain Computers*. GPO → Computer Configuration → Policies → Windows Settings → Scripts → **Startup** → add `\\fileserver\xephyguard\XephyGuard-Setup-latest.exe` with parameters `/S /KEY=<key> /CCC=192.168.10.2 /CAFP=<sha256>`. Re-running is harmless (in-place upgrade). |
| **Intune** | Win32 app (`IntuneWinAppUtil`); install command as above; detection rule: service `XephyGuardAgent` exists |
| **PDQ Deploy** | Install step with the silent command, run as an admin deploy user |
| **Imaging** | Run the silent install in the task sequence; the PC enrolls on first boot and stays Pending until approved |

#### 5-W.3 Prepare the PCs (GPO recommendations)
* Students use **standard accounts** (no local admin).
* Administrative Templates → Windows Components → **Windows Copilot** → *Turn off*. The installer
  sets it too.
* Import the Edge and Chrome ADMX templates and apply the AI and DNS-over-HTTPS policies from
  `installers/browser-policies/README.md`. The installer also writes them to the registry.
* Disable the *Windows Subsystem for Linux* and *Virtual Machine Platform* features, or leave them and
  rely on the `vm.*` alerts.
* Removable storage: according to lab policy (for exams: *All Removable Storage classes: Deny all access*).
* VS Code: machine-wide install with a read-only extensions folder
  (`--extensions-dir C:\ProgramData\XephyGuard\vscode-extensions`).
* **Do not** set a proxy on the PCs. In gateway mode, interception is transparent.

#### 5-W.4 What gets installed
* Service **XephyGuardAgent**: LocalSystem, starts automatically, restarts on failure, and only
  administrators can stop it.
* `xephyguard-helper.exe` in every user session (tray icon "Monitored by XephyGuard · mode: …"),
  started by the service. There is no Run key for students to delete.
* Lab CA in *Local Machine → Trusted Root*; data in `%ProgramData%\XephyGuard`, readable only by
  SYSTEM and Administrators.
* An outbound firewall rule for the agent; no inbound ports are opened.
* Browser policies (Copilot, Gemini, DoH and QUIC off), Windows Copilot off, and a login banner.
* Uninstalling needs administrator rights. Stopping the service during a session raises an alert.

#### 5-W.5 Check a PC (administrator PowerShell)
```powershell
& "C:\Program Files\XephyGuard\xephyguard-agent.exe" status
sc.exe query XephyGuardAgent
certutil -store Root | findstr /i xephyguard
```

---

## 6. Acceptance test (before the first class)

Do this with at least one Linux and one Windows PC (if you have both), logged in as a student.

| # | Step | Expected |
|---|---|---|
| 1 | `sudo xephyguard-ccc doctor` on the CCC | all checks OK |
| 2 | Dashboard → **Live lab** | every PC tile is **green** |
| 3 | Live lab → **Edit seat map** → place the tiles → **Save** | tiles match the room |
| 4 | Start a session with *Test: AI blocked* | PCs show "mode: Test" in the tray within seconds |
| 5 | On a PC, open `https://chatgpt.com` | blocked page (gateway/proxy-only only) |
| 6 | Search Google for "what is recursion" | no AI Overview (needs the lab CA on the PC) |
| 7 | On a PC, start a fake local LLM: Linux `python3 -m http.server 11434`, Windows `copy C:\Windows\System32\timeout.exe $env:TEMP\ollama.exe; & $env:TEMP\ollama.exe 60` | alert on the dashboard within seconds |
| 8 | Plug in a phone with USB tethering | network-adapter alert |
| 9 | Paste a large block of code into an editor | "large paste" signal in the PC drawer (no text is stored) |
| 10 | Unplug a PC's network cable for 20 s | *Machine silent* alert, then back to green |
| 11 | PC drawer → **Isolate** → **Restore network** | the PC loses Internet but stays connected to the CCC; restored afterwards |
| 12 | **End session** | blocking is lifted; a report with an explained score and PDF appears under **Reports** |

For research-grade numbers, run the red-team harness from the CCC (`docs/EVALUATION.md`):
```bash
export XG_INTERNAL_TOKEN=$(grep XG_INTERNAL_TOKEN /opt/xephyguard/ccc-deploy/.env | cut -d= -f2)
python3 eval/harness/redteam.py run --target student@192.168.10.21 \
        --scenarios eval/scenarios/linux.yaml --out eval/results/acceptance
python3 eval/harness/redteam.py table --out eval/results/acceptance
```

---

## 7. Running classes and tests

The one-page lab-assistant guide is `docs/LAB_ASSISTANT_GUIDE.md`. Print it for the lab desk. In
short:

* **Start:** Live lab → course name → profile → optional duration → **Start session**, or let the
  timetable do it.
* **Switch Practice ↔ Test:** choose the profile → **Switch**. PCs follow within seconds.
* **Watch:** tile colours (green / amber / red / grey). Click a tile for its timeline, alerts,
  processes, network, typing chart and pastes.
* **Act:** message one PC or all, close a program, isolate a PC, give an accommodation (per-PC
  override with a reason, which is logged).
* **End:** **End session** generates the report automatically.

**Every alert is a flag for human review, never an automatic verdict.**

---

## 8. Maintenance

| Task | Linux CCC / WSL2 | Native CCC |
|---|---|---|
| Health check | `sudo xephyguard-ccc doctor` | `php artisan xephyguard:doctor` |
| Logs | `sudo xephyguard-ccc logs web` (or `worker`, `reverb`, `proxy`, `netpolicy`, `analysis`) | `server/storage/logs/` |
| Backup (database, CA, secrets) | `sudo xephyguard-ccc backup /mnt/backup` | copy `server/database/database.sqlite`, `server/.env` and `server/storage/app/private/ca/` |
| Update the server | `cd /opt/xephyguard && sudo git pull && sudo xephyguard-ccc update` | `git pull`, `composer install`, `npm ci && npm run build`, `php artisan migrate` |
| Update agents | build a new release (Part 4) and publish it, then re-run the installer on the PCs (GPO/Ansible do this) | same |
| New enrollment key (e.g. after it leaked) | `sudo xephyguard-ccc enroll-key --rotate` | `php artisan xephyguard:enroll-key --rotate` |
| Large labs: partition the events table | `ccc-deploy/mysql/partition-events.sql` | – |

**Keep the CA safe.** `ca.key` signs every agent certificate and the detection bundles. It is
included in the backup, so store backups encrypted and offline.

**Every semester:**
- [ ] Check the browser AI policy names against the vendors' current docs
      (`installers/browser-policies/README.md`) and update them.
- [ ] Update the AI site blocklists and signatures (**Blocklists & signatures** → **Publish**).
- [ ] Run the acceptance test (Part 6) again.
- [ ] Check retention settings and delete data that is no longer needed.
- [ ] Rotate the enrollment key.

---

## 9. Troubleshooting

### Server
| Symptom | Fix |
|---|---|
| Browser certificate warning | install the lab CA on the admin machine (Part 3, step 1) |
| Dashboard does not update live | `xephyguard-ccc logs reverb`; reload the page. Native: is `reverb:start` running on 8081? |
| `doctor`: port 53 in use | `install.sh` disables the systemd-resolved stub; check for another DNS server (`sudo ss -lunp 'sport = :53'`) |
| `doctor`: IP forwarding off (gateway) | `sudo sysctl -w net.ipv4.ip_forward=1`; re-run `install.sh` |
| Report has no score / "scoring failed" | `xephyguard-ccc logs analysis`; then click **Re-score** on the report |
| WSL2: lab PCs cannot connect | mirrored mode not active (`wsl --shutdown` after editing `.wslconfig`), Hyper-V firewall rule missing, or the Windows IP changed |
| Native Windows: `could not find driver` | enable `pdo_sqlite` / `pdo_mysql` in `php.ini` |

### Lab PCs (both systems)
| Symptom | Fix |
|---|---|
| PC never appears as Pending | wrong key or CCC address; port 80/8443 blocked; CA fingerprint mismatch (the error message says which). Check with `xephyguard-agent status` |
| `cannot download the CA certificate` | the PC cannot reach `http://<CCC>/ca.crt` (firewall, wrong IP) |
| Tile is grey | PC off, agent stopped or network cut; during a session a *Machine silent* alert is raised as well |
| Websites not blocked in Test | the PC uses another gateway or DNS (check DHCP options 3/6), or the PC is not in the session; run `doctor` |
| Certificate warnings on websites | the lab CA is missing in that browser; reinstall the agent package or installer |
| Google still shows AI Overview | the lab CA is not trusted on the PC, or Google changed its page; update the fallback selectors in **Settings** |
| Too many "large paste" alerts | assignment templates pasted at the start; early pastes are already discounted; raise the threshold in **Settings** |

### Linux PC specific
| Symptom | Fix |
|---|---|
| "Cannot see" windows/clipboard | Wayland session; switch to X11 |
| Service not running | `journalctl -u xephyguard-agent -n 50` |
| Helper not running | log out and in; check `systemctl --user status xephyguard-helper` as the student |

### Windows PC specific
| Symptom | Fix |
|---|---|
| Installer finished but PC not Pending | it was installed without `/KEY`: run `xephyguard-agent.exe enroll ...` (5-W.1, step 3) |
| No tray icon | the helper restarts within ~15 s; a *helper missing* alert follows if it stays down; log the student out and in |
| SmartScreen warning | code-sign the installer (4-W step 4) or deploy through GPO/Intune |
| Service won't start | Event Viewer → Windows Logs → Application; log in `%ProgramData%\XephyGuard\logs\` (as administrator) |

---

## 10. Reference: ports, files, commands

### Ports on the CCC (lab side)
| Port | Purpose | Setups |
|---|---|---|
| TCP 80 | CA certificate, installers, student notice (`/notice`) | all |
| TCP 443 | dashboard | all |
| TCP 8443 | agents (TLS 1.3, client certificates) | all |
| UDP 47474 | agent auto-discovery (optional) | Docker setups |
| UDP/TCP 53 | DNS (Unbound) | Linux / WSL2 |
| TCP 8080 | transparent web filter (mitmproxy) | Linux / WSL2 |

Internal only (127.0.0.1): 3306 MySQL, 8000 internal API, 8081 Reverb, 8090 analysis.

### Important files on a Linux CCC
| Path | What |
|---|---|
| `/opt/xephyguard/ccc-deploy/.env` | secrets and settings (0600) |
| `/var/lib/xephyguard/ca/` | lab CA (`ca.key` is the most sensitive file in the system) |
| `/var/lib/xephyguard/mysql/` | database |
| `/var/lib/xephyguard/storage/public/downloads/` | agent installers served to PCs |
| `/var/log/xephyguard/` | logs |
| `/etc/nftables.d/xephyguard.nft` | gateway firewall rules |

### Commands
| Where | Command |
|---|---|
| CCC | `xephyguard-ccc doctor \| status \| logs <svc> \| enroll-key [--rotate] \| backup <dir> \| update \| artisan <cmd>` |
| Linux PC | `xephyguard-agent status \| scan \| enroll --key … [--ccc …] [--ca-fingerprint …] \| version` |
| Windows PC | `"C:\Program Files\XephyGuard\xephyguard-agent.exe" status \| scan \| enroll …` |

### Further reading
| Document | Contents |
|---|---|
| `docs/SETUP_CCC.md`, `docs/RUNNING_THE_SERVER.md` | server details and all four server setups |
| `docs/SETUP_LINUX_PC.md`, `docs/SETUP_WINDOWS_PC.md` | lab PC details |
| `docs/LAB_ASSISTANT_GUIDE.md` | one page for the lab desk |
| `docs/WINDOWS_TEST_CHECKLIST.md` | verify Windows PCs before relying on them |
| `docs/ETHICS.md` | privacy statement and consent template |
| `docs/THREAT_MODEL.md` | what XephyGuard can and cannot stop |
| `docs/EVALUATION.md` | red-team harness, metrics, research export |
| `docs/DECISIONS.md` | why things are built the way they are |
