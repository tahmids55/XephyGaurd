# ==============================================================================
# XephyGuard Native Server - Auto-Installer & Runner
# Checks dependencies (PHP, Nginx); downloads only if missing; launches server.
# ==============================================================================

[CmdletBinding()]
param(
    [string]$ServerIp = "",
    [switch]$ForceDownload = $false,
    [switch]$EnableAutoStart = $false,
    [switch]$DisableAutoStart = $false
)

# Check Administrator privileges (elevate if interactive, or continue gracefully)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Warning "Requesting Administrator privileges..."
        try {
            # FIX: pass the original parameters through to the elevated process
            $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
            if ($ServerIp)         { $argList += " -ServerIp `"$ServerIp`"" }
            if ($ForceDownload)    { $argList += " -ForceDownload" }
            if ($EnableAutoStart)  { $argList += " -EnableAutoStart" }
            if ($DisableAutoStart) { $argList += " -DisableAutoStart" }
            Start-Process powershell -Verb RunAs -ArgumentList $argList
            exit
        } catch {
            Write-Warning "Could not elevate automatically. Continuing without admin..."
        }
    } else {
        Write-Warning "Running non-elevated. Continuing (system firewall rule may be skipped)..."
    }
}

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$RepoRoot  = (Get-Item $PSScriptRoot).FullName
$ServerDir = Join-Path $RepoRoot "server"
$ToolsDir  = Join-Path $RepoRoot "tools"

# FIX: Nginx lives inside the project (tools\nginx) unless a working C:\nginx already exists.
# This avoids needing write access to the C:\ drive root.
if (Test-Path "C:\nginx\nginx.exe") {
    $NginxDir = "C:\nginx"
} else {
    $NginxDir = Join-Path $ToolsDir "nginx"
}

# ------------------------------------------------------------------------------
# Auto-Start Management
# ------------------------------------------------------------------------------
$AutoStartTaskName = "XephyGuardServer"
$StartupShortcut = Join-Path "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup" "XephyGuard-Server.lnk"

if ($DisableAutoStart) {
    if (Test-Path $StartupShortcut) {
        Remove-Item $StartupShortcut -Force
    }
    Unregister-ScheduledTask -TaskName $AutoStartTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "[OK] Windows Auto-Start has been DISABLED." -ForegroundColor Yellow
    exit 0
}

if ($EnableAutoStart) {
    try {
        $wsh = New-Object -ComObject WScript.Shell
        $sc = $wsh.CreateShortcut($StartupShortcut)
        $sc.TargetPath = "powershell.exe"
        $sc.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$RepoRoot\run-native.ps1`""
        $sc.WorkingDirectory = $RepoRoot
        $sc.Description = "XephyGuard Native Server Auto-Start"
        $sc.Save()
        Write-Host "[OK] Windows Auto-Start has been ENABLED!" -ForegroundColor Green
        Write-Host "    XephyGuard will start silently in the background whenever you log into Windows." -ForegroundColor Gray
    } catch {
        Write-Warning "Could not configure Auto-Start shortcut: $_"
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "           XephyGuard Native Server Launcher              " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Repository root: $RepoRoot" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 1. Detect Host LAN / Wi-Fi IP
# ------------------------------------------------------------------------------
if (-not $ServerIp) {
    $activeIp = $null
    $virtualPattern = "VMware|VirtualBox|Hyper-V|vEthernet|WSL|Loopback|TAP|VPN|Bluetooth"

    # Prefer the real adapter: it is connected and has a default gateway
    try {
        $activeIp = Get-NetIPConfiguration |
            Where-Object {
                $_.IPv4DefaultGateway -and
                $_.NetAdapter.Status -eq "Up" -and
                $_.InterfaceDescription -notmatch $virtualPattern -and
                $_.InterfaceAlias -notmatch $virtualPattern
            } |
            Select-Object -First 1 |
            ForEach-Object { $_.IPv4Address[0].IPAddress }
    } catch { $activeIp = $null }

    # Fallback: any non-virtual IPv4 address
    if (-not $activeIp) {
        $activeIp = Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object {
                $_.InterfaceAlias -notmatch $virtualPattern -and
                $_.IPAddress -notmatch "^169\.254|^127\."
            } | Select-Object -ExpandProperty IPAddress -First 1
    }

    $ServerIp = if ($activeIp) { $activeIp } else { "127.0.0.1" }
}
$parsedIp = $null
if (-not [System.Net.IPAddress]::TryParse($ServerIp, [ref]$parsedIp)) {
    Write-Host "[X] '$ServerIp' is not a valid IP address." -ForegroundColor Red
    Write-Host "    Use a real address such as 192.168.1.25, or omit -ServerIp to auto-detect." -ForegroundColor Yellow
    exit 1
}
Write-Host "[+] Target Server IP: $ServerIp" -ForegroundColor Green

# ------------------------------------------------------------------------------
# 2. Check & Install PHP (Requires >= 8.3)
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 1/5] Checking PHP (Requires PHP 8.3+) ---" -ForegroundColor Yellow

function Test-PhpVersion($path) {
    if (-not $path -or -not (Test-Path $path)) { return $null }
    try {
        $verId = & $path -r "echo PHP_VERSION_ID;" 2>$null
        $verStr = & $path -r "echo PHP_VERSION;" 2>$null
        if ($verId -and [int]$verId -ge 80300) {
            return @{ Path = $path; Version = $verStr; Valid = $true }
        } else {
            return @{ Path = $path; Version = $verStr; Valid = $false }
        }
    } catch {
        return $null
    }
}

$PhpExe = $null

# Priority 1: Check project-isolated tools\php
$localPhp = Join-Path $ToolsDir "php\php.exe"
$localCheck = Test-PhpVersion $localPhp
if ($localCheck -and $localCheck.Valid) {
    $PhpExe = $localCheck.Path
    Write-Host "[OK] Using isolated XephyGuard PHP $($localCheck.Version): $PhpExe" -ForegroundColor Green
}

# Priority 2: Check system/winget PHP
if (-not $PhpExe -and -not $ForceDownload) {
    $candidates = @()
    $cmdPhp = (Get-Command php.exe -ErrorAction SilentlyContinue).Source
    if ($cmdPhp) { $candidates += $cmdPhp }

    $wingetPhps = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "php.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
    if ($wingetPhps) { $candidates += $wingetPhps }

    foreach ($cand in ($candidates | Select-Object -Unique)) {
        $check = Test-PhpVersion $cand
        if ($check) {
            if ($check.Valid) {
                $PhpExe = $check.Path
                Write-Host "[OK] Found compatible PHP $($check.Version): $PhpExe (Skipping download)" -ForegroundColor Green
                break
            } else {
                Write-Warning "Found existing PHP at '$cand' but version is $($check.Version). XephyGuard requires PHP 8.3+."
            }
        }
    }
}

# Priority 3: Download/Install PHP 8.3 if missing or incompatible
if (-not $PhpExe -or $ForceDownload) {
    Write-Host "[*] No compatible PHP 8.3+ found. Installing PHP 8.3 via winget..." -ForegroundColor Cyan
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($winget) {
        & winget.exe install --id PHP.PHP.8.3 --exact --source winget --accept-source-agreements --accept-package-agreements --silent
        $wingetPhps = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "php.exe" -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        foreach ($cand in $wingetPhps) {
            $check = Test-PhpVersion $cand
            if ($check -and $check.Valid) { $PhpExe = $check.Path; break }
        }
    }

    if (-not $PhpExe) {
        Write-Error "Could not find or install PHP 8.3+. Please install PHP 8.3 or place it in $ToolsDir\php\."
        exit 1
    }
}

# Ensure required PHP extensions are enabled
$phpIniPath = Join-Path (Split-Path $PhpExe) "php.ini"
if (-not (Test-Path $phpIniPath)) {
    $prodIni = Join-Path (Split-Path $PhpExe) "php.ini-production"
    if (Test-Path $prodIni) {
        Copy-Item $prodIni $phpIniPath
    }
}
if (Test-Path $phpIniPath) {
    $iniContent = Get-Content $phpIniPath -Raw
    $neededExts = @("curl", "fileinfo", "gd", "intl", "mbstring", "openssl", "pdo_sqlite", "sqlite3", "zip")
    $changed = $false

    if ($iniContent -notmatch '(?m)^extension_dir\s*=\s*"ext"') {
        $iniContent = $iniContent -replace ';?extension_dir = "ext"', 'extension_dir = "ext"'
        $changed = $true
    }
    foreach ($ext in $neededExts) {
        if ($iniContent -match "(?m)^;extension=$ext\s*$") {
            $iniContent = $iniContent -replace "(?m)^;extension=$ext\s*$", "extension=$ext"
            $changed = $true
        }
    }
    if ($changed) {
        Set-Content -Path $phpIniPath -Value $iniContent
        Write-Host "[+] Updated php.ini to enable required extensions." -ForegroundColor Gray
    }
}

# ------------------------------------------------------------------------------
# 3. Check & Install Nginx for Windows
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 2/5] Checking Nginx ---" -ForegroundColor Yellow

$NginxExe = Join-Path $NginxDir "nginx.exe"
if (-not (Test-Path $NginxExe) -or $ForceDownload) {
    # FIX: install into the project's tools\nginx folder (no C:\ root access needed)
    $NginxDir = Join-Path $ToolsDir "nginx"
    $NginxExe = Join-Path $NginxDir "nginx.exe"
    Write-Host "[*] Nginx not found. Downloading Nginx 1.26.2 to $NginxDir..." -ForegroundColor Cyan

    $nginxZipUrl = "https://nginx.org/download/nginx-1.26.2.zip"
    $tempZip     = Join-Path $env:TEMP "nginx-1.26.2.zip"
    $tempExtract = Join-Path $env:TEMP "nginx-extract"

    # Stop any running nginx so files can be replaced
    Get-Process nginx -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    Invoke-WebRequest -Uri $nginxZipUrl -OutFile $tempZip -UseBasicParsing

    if (Test-Path $tempExtract) { Remove-Item $tempExtract -Recurse -Force }
    Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force

    New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null
    if (Test-Path $NginxDir) { Remove-Item $NginxDir -Recurse -Force }
    Move-Item -Path (Join-Path $tempExtract "nginx-1.26.2") -Destination $NginxDir

    Remove-Item $tempZip -Force
    Remove-Item $tempExtract -Recurse -Force
    Write-Host "[OK] Nginx installed to $NginxDir" -ForegroundColor Green
} else {
    Write-Host "[OK] Nginx is installed: $NginxExe (Skipping download)" -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 4. Configure Application Environment & Database
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 3/5] Configuring XephyGuard Environment ---" -ForegroundColor Yellow

$EnvFile = Join-Path $ServerDir ".env"
if (-not (Test-Path $EnvFile)) {
    Write-Host "[*] Creating fresh .env file..." -ForegroundColor Cyan
    Copy-Item (Join-Path $ServerDir ".env.example") $EnvFile

    # Generate random Reverb keys
    $revKey = -join ((65..90) + (97..122) | Get-Random -Count 16 | ForEach-Object {[char]$_})
    $revSec = -join ((65..90) + (97..122) | Get-Random -Count 24 | ForEach-Object {[char]$_})

    $envRaw = Get-Content $EnvFile -Raw
    $envRaw = $envRaw -replace "APP_URL=.*", "APP_URL=https://$ServerIp"
    $envRaw = $envRaw -replace "DB_CONNECTION=.*", "DB_CONNECTION=sqlite"
    $envRaw = $envRaw -replace "BROADCAST_CONNECTION=.*", "BROADCAST_CONNECTION=reverb"
    $envRaw = $envRaw -replace "REVERB_APP_KEY=.*", "REVERB_APP_KEY=$revKey"
    $envRaw = $envRaw -replace "REVERB_APP_SECRET=.*", "REVERB_APP_SECRET=$revSec"
    $envRaw = $envRaw -replace "REVERB_PORT=.*", "REVERB_PORT=8080"
    $envRaw = $envRaw -replace "REVERB_SERVER_PORT=.*", "REVERB_SERVER_PORT=8080"
    Set-Content -Path $EnvFile -Value $envRaw

    & $PhpExe "$ServerDir\artisan" key:generate --force
} else {
    Write-Host "[OK] .env file exists. Ensuring IP is up to date..." -ForegroundColor Green
    $envRaw = Get-Content $EnvFile -Raw
    if ($envRaw -notmatch "APP_URL=https://$([regex]::Escape($ServerIp))") {
        $envRaw = $envRaw -replace "APP_URL=https://[0-9\.]+", "APP_URL=https://$ServerIp"
        Set-Content -Path $EnvFile -Value $envRaw
    }
}

# Initialize SQLite database if missing
$sqlitePath = Join-Path $ServerDir "database\database.sqlite"
if (-not (Test-Path $sqlitePath)) {
    Write-Host "[*] Initializing SQLite database..." -ForegroundColor Cyan
    New-Item -ItemType File -Path $sqlitePath -Force | Out-Null
    & $PhpExe "$ServerDir\artisan" migrate --force
}

# Initialize Certificate Authority & Server TLS certificates
$caCertPath = Join-Path $ServerDir "storage\app\private\ca\server.crt"
if (-not (Test-Path $caCertPath)) {
    Write-Host "[*] Generating Lab Root CA and Server TLS Certificate..." -ForegroundColor Cyan
    & $PhpExe "$ServerDir\artisan" xephyguard:ca:init --server-ip=$ServerIp --server-dns=localhost --force
    & $PhpExe "$ServerDir\artisan" xephyguard:bootstrap
} else {
    Write-Host "[OK] CA certificates exist. Re-issuing for current IP ($ServerIp)..." -ForegroundColor Green
    & $PhpExe "$ServerDir\artisan" xephyguard:ca:init --server-ip=$ServerIp --server-dns=localhost
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[X] Certificate re-issue failed (see the error above). Stopping." -ForegroundColor Red
        exit 1
    }
}

# ------------------------------------------------------------------------------
# 5. Configure Nginx Reverse Proxy
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 4/5] Configuring Nginx ---" -ForegroundColor Yellow

$caDirFwd   = (Join-Path $ServerDir "storage\app\private\ca") -replace '\\', '/'
$publicFwd  = (Join-Path $ServerDir "public") -replace '\\', '/'
$confTarget = Join-Path $NginxDir "conf\xephyguard.conf"

$exampleConfPath = Join-Path $RepoRoot "ccc-deploy\nginx\dev-native.conf.example"
if (Test-Path $exampleConfPath) {
    $confText = Get-Content $exampleConfPath -Raw
    $confText = $confText.Replace("@PUBLIC@", $publicFwd).Replace("@CA_DIR@", $caDirFwd)
    $confText = $confText -replace "8081", "8080"
    Set-Content -Path $confTarget -Value $confText
    Write-Host "[+] Generated xephyguard.conf from template." -ForegroundColor Gray
}

# Write a clean main nginx.conf (original is backed up once as nginx.conf.orig).
# Patching the default file with regex is fragile: it can leave unbalanced braces,
# which makes Nginx refuse to start.
$mainConfPath = Join-Path $NginxDir "conf\nginx.conf"
if (Test-Path $mainConfPath) {
    $backupConf = "$mainConfPath.orig"
    if (-not (Test-Path $backupConf)) { Copy-Item $mainConfPath $backupConf }
    $mainConf = @"
worker_processes  1;

events {
    worker_connections  1024;
}

http {
    include       mime.types;
    default_type  application/octet-stream;
    sendfile      on;
    keepalive_timeout  65;
    client_max_body_size 100m;

    include xephyguard.conf;
}
"@
    Set-Content -Path $mainConfPath -Value $mainConf -Encoding ASCII
    Write-Host "[+] Wrote clean nginx.conf (original backed up as nginx.conf.orig)." -ForegroundColor Gray
}

# ------------------------------------------------------------------------------
# 6. Start / Restart Background Services
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 5/5] Launching Background Services ---" -ForegroundColor Yellow

# Configure Windows Firewall (optional, requires admin)
try {
    $ruleName = "XephyGuard Native Server"
    $fw = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
    if (-not $fw) {
        New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Protocol TCP -LocalPort 80,443,8443 -Action Allow -ErrorAction Stop | Out-Null
        Write-Host "[+] Windows Firewall rule created for ports 80, 443, 8443." -ForegroundColor Gray
    }
} catch {
    Write-Host "[!] Skipping Windows Firewall rule (run as Administrator if other PCs need access)." -ForegroundColor Gray
}

# Check for port conflicts before starting
$portsToCheck = @(
    @{ Port = 80;   Name = "HTTP / Web";      Expected = "nginx" },
    @{ Port = 443;  Name = "HTTPS Dashboard"; Expected = "nginx" },
    @{ Port = 8443; Name = "Agent mTLS API";  Expected = "nginx" }
)
foreach ($pt in $portsToCheck) {
    $conn = Get-NetTCPConnection -LocalPort $pt.Port -State Listen -ErrorAction SilentlyContinue
    if ($conn) {
        $owningProcId = ($conn | Select-Object -ExpandProperty OwningProcess -First 1)
        $proc = Get-Process -Id $owningProcId -ErrorAction SilentlyContinue
        if ($proc -and $proc.ProcessName -notmatch $pt.Expected) {
            Write-Warning "Port $($pt.Port) ($($pt.Name)) is currently occupied by '$($proc.ProcessName)' (PID $owningProcId)! If Nginx fails to bind, stop that service first."
        }
    }
}

# Validate the Nginx config before starting (native commands write to stderr, so relax error handling)
$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
Push-Location $NginxDir
$nginxTest = & $NginxExe -t 2>&1 | Out-String
Pop-Location
$ErrorActionPreference = $prevEap
if ($nginxTest -notmatch "test is successful") {
    Write-Host "[X] Nginx configuration test FAILED:" -ForegroundColor Red
    Write-Host $nginxTest -ForegroundColor Red
    Write-Host "    Fix the error above (config file: $confTarget), then run the script again." -ForegroundColor Yellow
    exit 1
}
Write-Host "[OK] Nginx configuration test passed." -ForegroundColor Green

# 1. Start Nginx
$nginxRunning = Get-Process nginx -ErrorAction SilentlyContinue
if ($nginxRunning) {
    Write-Host "[*] Reloading running Nginx..." -ForegroundColor Cyan
    Start-Process -FilePath $NginxExe -ArgumentList "-s reload" -WorkingDirectory $NginxDir -Wait -NoNewWindow
} else {
    Write-Host "[*] Starting Nginx..." -ForegroundColor Cyan
    Start-Process -FilePath $NginxExe -WorkingDirectory $NginxDir
}

# 2. Check & Start Laravel Web (Port 8000)
$p8000 = Get-NetTCPConnection -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue
if (-not $p8000) {
    Write-Host "[*] Starting Laravel Backend on 127.0.0.1:8000..." -ForegroundColor Cyan
    Start-Process -FilePath $PhpExe -ArgumentList "artisan serve --host=127.0.0.1 --port=8000" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "[OK] Laravel Backend already running on port 8000." -ForegroundColor Green
}

# 3. Check & Start Reverb WebSockets (Port 8080)
$p8080 = Get-NetTCPConnection -LocalPort 8080 -State Listen -ErrorAction SilentlyContinue
if (-not $p8080) {
    Write-Host "[*] Starting Reverb WebSockets on 127.0.0.1:8080..." -ForegroundColor Cyan
    Start-Process -FilePath $PhpExe -ArgumentList "artisan reverb:start --host=127.0.0.1 --port=8080" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "[OK] Reverb WebSockets already running on port 8080." -ForegroundColor Green
}

Start-Sleep -Seconds 3

# Verify that the services are really listening
$checks = @(
    @{ Port = 443;  Name = "Nginx HTTPS (dashboard)" },
    @{ Port = 80;   Name = "Nginx HTTP (CA download)" },
    @{ Port = 8443; Name = "Nginx mTLS (agent API)" },
    @{ Port = 8000; Name = "Laravel backend" },
    @{ Port = 8080; Name = "Reverb WebSockets" }
)
$anyDown = $false
foreach ($c in $checks) {
    $up = Get-NetTCPConnection -LocalPort $c.Port -State Listen -ErrorAction SilentlyContinue
    if ($up) {
        Write-Host "[OK] $($c.Name) is listening on port $($c.Port)." -ForegroundColor Green
    } else {
        Write-Host "[X] $($c.Name) is NOT listening on port $($c.Port)." -ForegroundColor Red
        $anyDown = $true
    }
}
if ($anyDown) {
    $errLog = Join-Path $NginxDir "logs\error.log"
    if (Test-Path $errLog) {
        Write-Host "`n--- Last lines of Nginx error.log ---" -ForegroundColor Yellow
        Get-Content $errLog -Tail 15 | ForEach-Object { Write-Host $_ -ForegroundColor Gray }
    }
    Write-Warning "Some services are not running. Check the messages above before opening the dashboard."
}

# ------------------------------------------------------------------------------
# 7. Verification & Summary
# ------------------------------------------------------------------------------
Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "       XephyGuard Native Server is RUNNING!               " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Dashboard URL:      https://$ServerIp/ (or https://localhost/)" -ForegroundColor Yellow
Write-Host "  Agent API Port:     8443 (mTLS secured)" -ForegroundColor Gray
Write-Host "  Downloads & CA:     http://$ServerIp/ca.crt" -ForegroundColor Gray
Write-Host ""
Write-Host "  To open in clean Desktop App window, press Win+R and run:" -ForegroundColor Cyan
Write-Host "  msedge.exe --app=https://$ServerIp/" -ForegroundColor White
Write-Host ""