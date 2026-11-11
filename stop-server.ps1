# ==============================================================================
# XephyGuard Server Terminator
# Stops Nginx, Laravel Web, Reverb WebSockets, and Scheduler processes.
# ==============================================================================

Write-Host "Stopping all XephyGuard server services..." -ForegroundColor Yellow

# 1. Stop Nginx
$nginx = Get-Process nginx -ErrorAction SilentlyContinue
if ($nginx) {
    Write-Host "  -> Terminating Nginx processes..." -ForegroundColor Cyan
    $nginx | Stop-Process -Force -ErrorAction SilentlyContinue
}

# 2. Stop Laravel on 8000, Reverb on 8080, and Screen Gateway on 8082
@(8000, 8080, 8082) | ForEach-Object {
    $conn = Get-NetTCPConnection -LocalPort $_ -State Listen -ErrorAction SilentlyContinue
    if ($conn) {
        $pids = $conn | Select-Object -ExpandProperty OwningProcess -Unique
        foreach ($p in $pids) {
            Write-Host "  -> Stopping process $p on port $_..." -ForegroundColor Cyan
            Stop-Process -Id $p -Force -ErrorAction SilentlyContinue
        }
    }
}

# 3. Stop gateway.exe if still active
Get-Process gateway -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "  -> Stopping screen gateway process: $($_.Id)" -ForegroundColor Cyan
    Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
}

# 4. Stop background schedule:work
Get-WmiObject Win32_Process -Filter "CommandLine LIKE '%artisan%'" -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.CommandLine -match "schedule:work|serve|reverb") {
        Write-Host "  -> Stopping artisan job: $($_.ProcessId)" -ForegroundColor Cyan
        Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "`n[OK] All XephyGuard server services stopped successfully." -ForegroundColor Green
