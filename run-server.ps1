# ==============================================================================
# XephyGuard Server Launcher (Fast Dedicated Runner)
# Starts Nginx, Laravel Web, Reverb WebSockets, and Background Scheduler.
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$Restart = $false
)

$ErrorActionPreference = "Continue"

$RepoRoot  = (Get-Item $PSScriptRoot).FullName
$ServerDir = Join-Path $RepoRoot "server"
$NginxExe  = if (Test-Path "C:\nginx\nginx.exe") { "C:\nginx\nginx.exe" } else { Join-Path $RepoRoot "tools\nginx\nginx.exe" }
$NginxDir  = Split-Path $NginxExe -Parent

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "          XephyGuard Dedicated Server Launcher            " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Server directory: $ServerDir" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 1. Detect LAN IP
# ------------------------------------------------------------------------------
$LanIp = "127.0.0.1"
try {
    $ipCandidate = Get-NetIPAddress -AddressFamily IPv4 |
        Where-Object { 
            $_.InterfaceAlias -notmatch "vEthernet|Loopback|VirtualBox|VMware|Bluetooth" -and 
            $_.IPAddress -notlike "127.*" -and 
            $_.IPAddress -notlike "169.254.*" 
        } | Select-Object -First 1

    if ($ipCandidate) {
        $LanIp = $ipCandidate.IPAddress
    }
} catch { }

# ------------------------------------------------------------------------------
# 2. Restart mode (if requested, stop existing first)
# ------------------------------------------------------------------------------
if ($Restart) {
    Write-Host "[*] Restart flag detected. Stopping running server processes..." -ForegroundColor Yellow
    Get-Process nginx -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process gateway -ErrorAction SilentlyContinue | Stop-Process -Force
    @(8000, 8080, 8082) | ForEach-Object {
        $conn = Get-NetTCPConnection -LocalPort $_ -State Listen -ErrorAction SilentlyContinue
        if ($conn) {
            $pidToKill = $conn | Select-Object -ExpandProperty OwningProcess -First 1
            Stop-Process -Id $pidToKill -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Seconds 1
}

# ------------------------------------------------------------------------------
# 3. Start Nginx
# ------------------------------------------------------------------------------
Write-Host "`n[1/4] Checking Nginx..." -ForegroundColor Yellow
if (Test-Path $NginxExe) {
    $nginxProc = Get-Process nginx -ErrorAction SilentlyContinue
    if (-not $nginxProc) {
        Write-Host "  -> Starting Nginx ($NginxExe)..." -ForegroundColor Cyan
        Start-Process -FilePath $NginxExe -WorkingDirectory $NginxDir
    } else {
        Write-Host "  -> Reloading running Nginx..." -ForegroundColor Cyan
        Start-Process -FilePath $NginxExe -ArgumentList "-s reload" -WorkingDirectory $NginxDir -Wait -NoNewWindow
    }
} else {
    Write-Host "  -> Nginx binary not found. Skipping (Laravel serves directly)." -ForegroundColor DarkGray
}

# ------------------------------------------------------------------------------
# 4. Start Laravel Web Backend (Port 8000)
# ------------------------------------------------------------------------------
Write-Host "`n[2/4] Checking Laravel Web Backend (Port 8000)..." -ForegroundColor Yellow
$p8000 = Get-NetTCPConnection -LocalPort 8000 -State Listen -ErrorAction SilentlyContinue
if (-not $p8000) {
    Write-Host "  -> Starting Laravel on 0.0.0.0:8000..." -ForegroundColor Cyan
    Start-Process -FilePath "php" -ArgumentList "artisan serve --host=0.0.0.0 --port=8000" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Laravel Backend already active on port 8000." -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 5. Start Laravel Reverb WebSocket Server (Port 8080)
# ------------------------------------------------------------------------------
Write-Host "`n[3/5] Checking Reverb WebSockets (Port 8080)..." -ForegroundColor Yellow
$p8080 = Get-NetTCPConnection -LocalPort 8080 -State Listen -ErrorAction SilentlyContinue
if (-not $p8080) {
    Write-Host "  -> Starting Reverb on 0.0.0.0:8080..." -ForegroundColor Cyan
    Start-Process -FilePath "php" -ArgumentList "artisan reverb:start --host=0.0.0.0 --port=8080" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Reverb WebSockets already active on port 8080." -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 6. Start Screen View Gateway (Port 8082)
# ------------------------------------------------------------------------------
Write-Host "`n[4/5] Checking Screen View Gateway (Port 8082)..." -ForegroundColor Yellow
$p8082 = Get-NetTCPConnection -LocalPort 8082 -State Listen -ErrorAction SilentlyContinue
if (-not $p8082) {
    $gatewayExe = Join-Path $RepoRoot "xephyguard-screen\gateway\gateway.exe"
    $gatewayDir = Join-Path $RepoRoot "xephyguard-screen\gateway"
    if (Test-Path $gatewayExe) {
        Write-Host "  -> Starting Screen Gateway binary on :8082..." -ForegroundColor Cyan
        Start-Process -FilePath $gatewayExe -ArgumentList "-addr :8082" -WorkingDirectory $gatewayDir -WindowStyle Hidden
    } else {
        Write-Host "  -> Starting Screen Gateway via 'go run' on :8082..." -ForegroundColor Cyan
        Start-Process -FilePath "go" -ArgumentList "run main.go relay.go auth.go auth_iface.go -addr :8082" -WorkingDirectory $gatewayDir -WindowStyle Hidden
    }
} else {
    Write-Host "  -> Screen View Gateway already active on port 8082." -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 7. Start Heartbeat Watchdog & Scheduler (detectSilent)
# ------------------------------------------------------------------------------
Write-Host "`n[5/5] Checking Heartbeat & Tamper Watchdog..." -ForegroundColor Yellow
$schedCheck = Get-WmiObject Win32_Process -Filter "CommandLine LIKE '%artisan schedule:work%'" -ErrorAction SilentlyContinue
if (-not $schedCheck) {
    Write-Host "  -> Starting Background Scheduler (Heartbeat Watchdog)..." -ForegroundColor Cyan
    Start-Process -FilePath "php" -ArgumentList "artisan schedule:work" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Background Scheduler already active." -ForegroundColor Green
}

Start-Sleep -Seconds 2

# ------------------------------------------------------------------------------
# 8. Verification & Service Status
# ------------------------------------------------------------------------------
Write-Host "`n--- Live Service Health Status ---" -ForegroundColor Yellow

$services = @(
    @{ Name = "Laravel Dashboard HTTP"; Port = 8000 },
    @{ Name = "Reverb WebSockets";      Port = 8080 },
    @{ Name = "Screen View Gateway";    Port = 8082 },
    @{ Name = "Nginx HTTPS (SSL)";      Port = 443 },
    @{ Name = "Nginx Agent mTLS";       Port = 8443 },
    @{ Name = "Nginx HTTP (Port 80)";   Port = 80 }
)

foreach ($s in $services) {
    $listening = Get-NetTCPConnection -LocalPort $s.Port -State Listen -ErrorAction SilentlyContinue
    if ($listening) {
        Write-Host "  [OK] $($s.Name) listening on port $($s.Port)" -ForegroundColor Green
    } else {
        if ($s.Port -in @(80, 443, 8443)) {
            Write-Host "  [-] $($s.Name) not active (direct port 8000 available)" -ForegroundColor DarkGray
        } else {
            Write-Host "  [X] $($s.Name) NOT listening on port $($s.Port)" -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "          XephyGuard Server is READY & ONLINE!            " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Teacher Local Dashboard:  http://localhost:8000/" -ForegroundColor Yellow
Write-Host "  Teacher LAN Dashboard:    http://${LanIp}:8000/ (or https://${LanIp}/)" -ForegroundColor Yellow
Write-Host "  Student Downloads Portal: http://${LanIp}:8000/downloads" -ForegroundColor White
Write-Host "  Live Screen Streaming:    ws://${LanIp}:8082/" -ForegroundColor Cyan
Write-Host "  Events WebSocket:         ws://${LanIp}:8080/" -ForegroundColor Gray
Write-Host ""
Write-Host "  To stop all servers anytime, double-click: stop-server.bat" -ForegroundColor Cyan
Write-Host "  (or run: powershell .\stop-server.ps1)" -ForegroundColor Cyan
Write-Host ""
