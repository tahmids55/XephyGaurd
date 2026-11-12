# ==============================================================================
# XephyGuard Server Launcher (Windows)
# Starts Nginx (if installed), Laravel HTTP, Reverb WebSockets,
# Screen View Gateway, and Background Scheduler.
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$Restart = $false,
    [int]$AppPort = 0,
    [int]$ReverbPort = 0,
    [int]$GatewayPort = 0,
    [int]$HttpPort = 80,
    [int]$HttpsPort = 443,
    [int]$MtlsPort = 8443,
    [string]$NginxDir = "",
    [switch]$SkipNginx = $false
)

. (Join-Path $PSScriptRoot "_common.ps1")

$ErrorActionPreference = "Continue"
$RepoRoot  = Get-XephyRepoRoot
$ServerDir = Join-Path $RepoRoot "server"
$EnvFile   = Join-Path $ServerDir ".env"

if (-not (Test-Path $EnvFile)) {
    Write-Host "[!] Missing $EnvFile." -ForegroundColor Red
    Write-Host "    Run first-time setup first: .\new_setup_scripts\windows\setup-server.ps1" -ForegroundColor Yellow
    exit 1
}

$PhpExe = Find-XephyPhp -RepoRoot $RepoRoot
if (-not $PhpExe) {
    Write-Host "[!] Compatible PHP 8.3+ not found. Run .\new_setup_scripts\windows\setup-server.ps1 first." -ForegroundColor Red
    exit 1
}

$NginxDir = Find-XephyNginx -RepoRoot $RepoRoot -PreferredDir $NginxDir
$NginxExe = Join-Path $NginxDir "nginx.exe"

$envMap = Read-DotEnvMap -Path $EnvFile
if ($AppPort -le 0)     { $AppPort     = if ($envMap["APP_PORT"] -match '^\d+$')            { [int]$envMap["APP_PORT"] }            else { 8000 } }
if ($ReverbPort -le 0)  { $ReverbPort  = if ($envMap["REVERB_PORT"] -match '^\d+$')         { [int]$envMap["REVERB_PORT"] }         else { 8080 } }
if ($GatewayPort -le 0) { $GatewayPort = if ($envMap["SCREEN_GATEWAY_PORT"] -match '^\d+$') { [int]$envMap["SCREEN_GATEWAY_PORT"] } else { 8082 } }

$envRaw = Get-Content $EnvFile -Raw
$envChanged = $false
$gwSecret = $envMap["SCREEN_GATEWAY_SECRET"]
if (-not $gwSecret -or $gwSecret.StartsWith("dev-secret")) {
    $gwSecret = New-RandomHex 32
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_SECRET" -Value $gwSecret
    $envChanged = $true
}
$gwAgentSecret = $envMap["SCREEN_GATEWAY_AGENT_SECRET"]
if (-not $gwAgentSecret -or $gwAgentSecret.StartsWith("dev-secret")) {
    $gwAgentSecret = New-RandomHex 32
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_AGENT_SECRET" -Value $gwAgentSecret
    $envChanged = $true
}
if ($envChanged) {
    Set-Content -Path $EnvFile -Value $envRaw -NoNewline
    $envMap = Read-DotEnvMap -Path $EnvFile
}
$gwIssuer   = if ($envMap["SCREEN_GATEWAY_ISSUER"])   { $envMap["SCREEN_GATEWAY_ISSUER"] }   else { "xephyguard-ccc" }
$gwAudience = if ($envMap["SCREEN_GATEWAY_AUDIENCE"]) { $envMap["SCREEN_GATEWAY_AUDIENCE"] } else { "xephyguard-gateway" }

$LanIp = Get-XephyLanIp -Fallback "127.0.0.1"

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "          XephyGuard Server Launcher (Windows)            " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Repository root: $RepoRoot" -ForegroundColor Gray
Write-Host "PHP binary:      $PhpExe" -ForegroundColor Gray
Write-Host "Detected LAN IP: $LanIp" -ForegroundColor Gray

if ($Restart) {
    Write-Host "`n[*] Restart requested: stopping existing server processes..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "stop-server.ps1") -Quiet
    Start-Sleep -Seconds 1
}

# 1. Nginx
Write-Host "`n[1/5] Checking Nginx..." -ForegroundColor Yellow
if (-not $SkipNginx -and (Test-Path $NginxExe)) {
    $nginxProc = Get-Process nginx -ErrorAction SilentlyContinue
    if (-not $nginxProc) {
        Write-Host "  -> Starting Nginx ($NginxExe)..." -ForegroundColor Cyan
        Start-Process -FilePath $NginxExe -WorkingDirectory $NginxDir
    } else {
        Write-Host "  -> Reloading running Nginx..." -ForegroundColor Cyan
        Start-Process -FilePath $NginxExe -ArgumentList "-s reload" -WorkingDirectory $NginxDir -Wait -NoNewWindow
    }
} else {
    Write-Host "  -> Nginx skipped or not installed (direct HTTP port $AppPort active)." -ForegroundColor DarkGray
}

# 2. Laravel Backend
Write-Host "`n[2/5] Checking Laravel Web Backend (Port $AppPort)..." -ForegroundColor Yellow
$pApp = Get-NetTCPConnection -LocalPort $AppPort -State Listen -ErrorAction SilentlyContinue
if (-not $pApp) {
    Write-Host "  -> Starting Laravel on 0.0.0.0:$AppPort..." -ForegroundColor Cyan
    Start-Process -FilePath $PhpExe -ArgumentList "artisan serve --host=0.0.0.0 --port=$AppPort" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Laravel Backend already active on port $AppPort." -ForegroundColor Green
}

# 3. Reverb WebSockets
Write-Host "`n[3/5] Checking Reverb WebSockets (Port $ReverbPort)..." -ForegroundColor Yellow
$pReverb = Get-NetTCPConnection -LocalPort $ReverbPort -State Listen -ErrorAction SilentlyContinue
if (-not $pReverb) {
    Write-Host "  -> Starting Reverb on 0.0.0.0:$ReverbPort..." -ForegroundColor Cyan
    Start-Process -FilePath $PhpExe -ArgumentList "artisan reverb:start --host=0.0.0.0 --port=$ReverbPort" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Reverb WebSockets already active on port $ReverbPort." -ForegroundColor Green
}

# 4. Screen View Gateway
Write-Host "`n[4/5] Checking Screen View Gateway (Port $GatewayPort)..." -ForegroundColor Yellow
$pGateway = Get-NetTCPConnection -LocalPort $GatewayPort -State Listen -ErrorAction SilentlyContinue
if (-not $pGateway) {
    $gatewayDir = Join-Path $RepoRoot "xephyguard-screen\gateway"
    $gatewayExe = Join-Path $gatewayDir "gateway.exe"
    $env:SCREEN_GATEWAY_ADDR         = ":$GatewayPort"
    $env:SCREEN_GATEWAY_SECRET       = $gwSecret
    $env:SCREEN_GATEWAY_AGENT_SECRET = $gwAgentSecret
    $env:SCREEN_GATEWAY_ISSUER       = $gwIssuer
    $env:SCREEN_GATEWAY_AUDIENCE     = $gwAudience

    $goCmd = Get-Command go -ErrorAction SilentlyContinue
    if ($goCmd) {
        $latestSrc = Get-ChildItem -Path $gatewayDir -Filter "*.go" -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not (Test-Path $gatewayExe) -or ($latestSrc -and $latestSrc.LastWriteTime -gt (Get-Item $gatewayExe).LastWriteTime)) {
            Write-Host "  -> Building updated gateway.exe..." -ForegroundColor Cyan
            Push-Location $gatewayDir
            & go build -o $gatewayExe .
            Pop-Location
        }
    }

    if (Test-Path $gatewayExe) {
        Write-Host "  -> Starting Screen Gateway binary on :$GatewayPort..." -ForegroundColor Cyan
        Start-Process -FilePath $gatewayExe -ArgumentList "-addr :$GatewayPort" -WorkingDirectory $gatewayDir -WindowStyle Hidden
    } elseif ($goCmd) {
        Write-Host "  -> Starting Screen Gateway via 'go run' on :$GatewayPort..." -ForegroundColor Cyan
        Start-Process -FilePath "go" -ArgumentList "run . -addr :$GatewayPort" -WorkingDirectory $gatewayDir -WindowStyle Hidden
    } else {
        Write-Warning "Neither gateway.exe nor Go found in $gatewayDir."
    }
} else {
    Write-Host "  -> Screen View Gateway already active on port $GatewayPort." -ForegroundColor Green
}

# 5. Scheduler
Write-Host "`n[5/5] Checking Heartbeat & Tamper Watchdog..." -ForegroundColor Yellow
$schedCheck = Get-CimInstance Win32_Process -Filter "CommandLine LIKE '%artisan schedule:work%'" -ErrorAction SilentlyContinue
if (-not $schedCheck) {
    Write-Host "  -> Starting Background Scheduler (Heartbeat Watchdog)..." -ForegroundColor Cyan
    Start-Process -FilePath $PhpExe -ArgumentList "artisan schedule:work" -WorkingDirectory $ServerDir -WindowStyle Hidden
} else {
    Write-Host "  -> Background Scheduler already active." -ForegroundColor Green
}

Start-Sleep -Seconds 2

& (Join-Path $PSScriptRoot "status-server.ps1") -AppPort $AppPort -ReverbPort $ReverbPort -GatewayPort $GatewayPort -HttpPort $HttpPort -HttpsPort $HttpsPort -MtlsPort $MtlsPort
