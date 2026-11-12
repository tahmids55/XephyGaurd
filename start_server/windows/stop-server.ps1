# ==============================================================================
# XephyGuard Server Terminator (Windows)
# Stops Nginx, Laravel HTTP, Reverb WebSockets, Screen Gateway, and Scheduler.
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$Quiet = $false
)

. (Join-Path $PSScriptRoot "_common.ps1")

$RepoRoot  = Get-XephyRepoRoot
$ServerDir = Join-Path $RepoRoot "server"
$EnvFile   = Join-Path $ServerDir ".env"

if (-not $Quiet) {
    Write-Host "Stopping all XephyGuard server services..." -ForegroundColor Yellow
}

# 1. Stop Nginx
$nginx = Get-Process nginx -ErrorAction SilentlyContinue
if ($nginx) {
    if (-not $Quiet) { Write-Host "  -> Terminating Nginx processes..." -ForegroundColor Cyan }
    $nginx | Stop-Process -Force -ErrorAction SilentlyContinue
}

# 2. Stop Laravel, Reverb, and Screen Gateway on configured ports
$envMap      = Read-DotEnvMap -Path $EnvFile
$appPort     = if ($envMap["APP_PORT"] -match '^\d+$')            { [int]$envMap["APP_PORT"] }            else { 8000 }
$reverbPort  = if ($envMap["REVERB_PORT"] -match '^\d+$')         { [int]$envMap["REVERB_PORT"] }         else { 8080 }
$gatewayPort = if ($envMap["SCREEN_GATEWAY_PORT"] -match '^\d+$') { [int]$envMap["SCREEN_GATEWAY_PORT"] } else { 8082 }

@($appPort, $reverbPort, $gatewayPort) | Select-Object -Unique | ForEach-Object {
    $conn = Get-NetTCPConnection -LocalPort $_ -State Listen -ErrorAction SilentlyContinue
    if ($conn) {
        $pids = $conn | Select-Object -ExpandProperty OwningProcess -Unique
        foreach ($p in $pids) {
            if (-not $Quiet) { Write-Host "  -> Stopping process $p on port $_..." -ForegroundColor Cyan }
            Stop-Process -Id $p -Force -ErrorAction SilentlyContinue
        }
    }
}

# 3. Stop gateway.exe if still active
Get-Process gateway -ErrorAction SilentlyContinue | ForEach-Object {
    if (-not $Quiet) { Write-Host "  -> Stopping screen gateway process: $($_.Id)" -ForegroundColor Cyan }
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}

# 4. Stop background artisan workers matching this server repo
Get-CimInstance Win32_Process -Filter "CommandLine LIKE '%artisan%'" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.CommandLine -match "schedule:work|serve|reverb") {
        if (-not $Quiet) { Write-Host "  -> Stopping artisan job: $($_.ProcessId)" -ForegroundColor Cyan }
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

if (-not $Quiet) {
    Write-Host "`n[OK] All XephyGuard server services stopped." -ForegroundColor Green
}
