# ==============================================================================
# XephyGuard Server Status Check (Windows)
# ==============================================================================

[CmdletBinding()]
param(
    [int]$AppPort = 0,
    [int]$ReverbPort = 0,
    [int]$GatewayPort = 0,
    [int]$HttpPort = 80,
    [int]$HttpsPort = 443,
    [int]$MtlsPort = 8443
)

. (Join-Path $PSScriptRoot "_common.ps1")

$RepoRoot  = Get-XephyRepoRoot
$ServerDir = Join-Path $RepoRoot "server"
$EnvFile   = Join-Path $ServerDir ".env"
$envMap    = Read-DotEnvMap -Path $EnvFile

if ($AppPort -le 0)     { $AppPort     = if ($envMap["APP_PORT"] -match '^\d+$')            { [int]$envMap["APP_PORT"] }            else { 8000 } }
if ($ReverbPort -le 0)  { $ReverbPort  = if ($envMap["REVERB_PORT"] -match '^\d+$')         { [int]$envMap["REVERB_PORT"] }         else { 8080 } }
if ($GatewayPort -le 0) { $GatewayPort = if ($envMap["SCREEN_GATEWAY_PORT"] -match '^\d+$') { [int]$envMap["SCREEN_GATEWAY_PORT"] } else { 8082 } }

$LanIp = Get-XephyLanIp -Fallback "127.0.0.1"
$cafp  = Get-XephyCaFingerprint -ServerDir $ServerDir

Write-Host "`n--- XephyGuard Live Service Status (Windows) ---" -ForegroundColor Yellow

$services = @(
    @{ Name = "Laravel Dashboard HTTP"; Port = $AppPort;     Optional = $false },
    @{ Name = "Reverb WebSockets";      Port = $ReverbPort;  Optional = $false },
    @{ Name = "Screen View Gateway";    Port = $GatewayPort; Optional = $false },
    @{ Name = "Nginx HTTPS (SSL)";      Port = $HttpsPort;   Optional = $true },
    @{ Name = "Nginx Agent mTLS";       Port = $MtlsPort;    Optional = $true },
    @{ Name = "Nginx HTTP";             Port = $HttpPort;    Optional = $true }
)

foreach ($s in $services) {
    $listening = Get-NetTCPConnection -LocalPort $s.Port -State Listen -ErrorAction SilentlyContinue
    if ($listening) {
        Write-Host "  [OK] $($s.Name) listening on port $($s.Port)" -ForegroundColor Green
    } elseif ($s.Optional) {
        Write-Host "  [-] $($s.Name) not active (direct port $AppPort available)" -ForegroundColor DarkGray
    } else {
        Write-Host "  [X] $($s.Name) NOT listening on port $($s.Port)" -ForegroundColor Red
    }
}

$sched = Get-CimInstance Win32_Process -Filter "CommandLine LIKE '%artisan schedule:work%'" -ErrorAction SilentlyContinue
if ($sched) {
    Write-Host "  [OK] Background Scheduler running (PID $($sched.ProcessId))" -ForegroundColor Green
} else {
    Write-Host "  [X] Background Scheduler NOT running" -ForegroundColor Red
}

$httpsSuffix = if ($HttpsPort -eq 443) { "" } else { ":$HttpsPort" }
$httpSuffix  = if ($HttpPort -eq 80)   { "" } else { ":$HttpPort" }
Write-Host ""
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  Dashboard (Local):   http://localhost:$AppPort/ (or https://localhost$httpsSuffix/)" -ForegroundColor Yellow
Write-Host "  Dashboard (LAN):     http://${LanIp}:${AppPort}/ (or https://${LanIp}${httpsSuffix}/)" -ForegroundColor Yellow
Write-Host "  Student Downloads:   http://${LanIp}:${AppPort}/downloads (Nginx: http://${LanIp}${httpSuffix}/downloads)" -ForegroundColor White
Write-Host "  Live Screen Relay:   ws://${LanIp}:${GatewayPort}/ (or wss://${LanIp}${httpsSuffix}/screen-gw/)" -ForegroundColor Cyan
Write-Host "  Events WebSocket:    ws://${LanIp}:${ReverbPort}/" -ForegroundColor Gray
if ($cafp) {
    Write-Host "  CA SHA-256:          $cafp" -ForegroundColor Gray
}
Write-Host "==========================================================" -ForegroundColor Green
Write-Host "  To stop: .\start_server\windows\stop-server.ps1 (or double-click stop-server.bat)" -ForegroundColor Cyan
Write-Host ""
