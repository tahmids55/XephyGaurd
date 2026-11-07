<#
.SYNOPSIS
    XephyGuard CCC Windows Production Service Wrapper (P1-C5)

.DESCRIPTION
    Manages the XephyGuard Central Control Computer (CCC) stack on Windows Server / Windows 11
    as resilient background services with automatic restart on failure:
      - xg-ccc-web        (PHP/Laravel HTTP server on 127.0.0.1:8000)
      - xg-ccc-reverb     (Laravel Reverb WebSocket server on 127.0.0.1:8081)
      - xg-ccc-worker     (Laravel queue worker for scoring and reports)
      - xg-ccc-scheduler  (Laravel task scheduler for timetables, silent-PC detection, and lease checks)
      - xg-ccc-analysis   (Python behavioral/scoring service on 127.0.0.1:8090)
      - xg-ccc-nginx      (Nginx TLS 1.3 + mTLS reverse proxy on ports 80, 443, and 8443)

    Uses NSSM (Non-Sucking Service Manager) when available on PATH to register native Windows Services
    configured with automatic failure recovery (`sc.exe failure ... reset= 60 actions= restart/5000`),
    or falls back to elevated SYSTEM Scheduled Tasks with an automatic 1-minute watchdog restart policy.

.PARAMETER Action
    One of: install, uninstall, start, stop, restart, status, health

.PARAMETER RootDir
    Path to the XephyGuard repository root (default: parent of the scripts directory).

.PARAMETER PhpExe
    Path to php.exe (default: C:\php\php.exe or php.exe on PATH).

.PARAMETER NginxDir
    Path to the nginx installation directory (default: C:\nginx).

.PARAMETER PythonExe
    Path to the Python interpreter for the analysis service (default: <RootDir>\analysis\.venv\Scripts\python.exe).

.EXAMPLE
    .\scripts\windows-ccc-service.ps1 install -RootDir C:\xephyguard
    .\scripts\windows-ccc-service.ps1 start
    .\scripts\windows-ccc-service.ps1 status
    .\scripts\windows-ccc-service.ps1 health
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet("install", "uninstall", "start", "stop", "restart", "status", "health")]
    [string]$Action,

    [string]$RootDir = (Split-Path -Parent $PSScriptRoot),
    [string]$PhpExe = "C:\php\php.exe",
    [string]$NginxDir = "C:\nginx",
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"

$ServerDir = Join-Path $RootDir "server"
$AnalysisDir = Join-Path $RootDir "analysis"
$LogDir = Join-Path $ServerDir "storage\logs"
if (-not $PythonExe) {
    $VenvPy = Join-Path $AnalysisDir ".venv\Scripts\python.exe"
    $PythonExe = if (Test-Path $VenvPy) { $VenvPy } else { "python.exe" }
}
if (-not (Test-Path $PhpExe)) {
    $PhpCmd = Get-Command php.exe -ErrorAction SilentlyContinue
    if ($PhpCmd) { $PhpExe = $PhpCmd.Source }
}
$NginxExe = Join-Path $NginxDir "nginx.exe"

$Services = @(
    @{
        Name        = "xg-ccc-web"
        DisplayName = "XephyGuard CCC Web (Laravel)"
        WorkDir     = $ServerDir
        Exe         = $PhpExe
        Args        = "artisan serve --host=127.0.0.1 --port=8000"
        Port        = 8000
    },
    @{
        Name        = "xg-ccc-reverb"
        DisplayName = "XephyGuard CCC Reverb (WebSocket)"
        WorkDir     = $ServerDir
        Exe         = $PhpExe
        Args        = "artisan reverb:start --host=127.0.0.1 --port=8081"
        Port        = 8081
    },
    @{
        Name        = "xg-ccc-worker"
        DisplayName = "XephyGuard CCC Queue Worker"
        WorkDir     = $ServerDir
        Exe         = $PhpExe
        Args        = "artisan queue:work --tries=3 --timeout=120"
        Port        = 0
    },
    @{
        Name        = "xg-ccc-scheduler"
        DisplayName = "XephyGuard CCC Scheduler"
        WorkDir     = $ServerDir
        Exe         = $PhpExe
        Args        = "artisan schedule:work"
        Port        = 0
    },
    @{
        Name        = "xg-ccc-analysis"
        DisplayName = "XephyGuard CCC Analysis Service"
        WorkDir     = $AnalysisDir
        Exe         = $PythonExe
        Args        = "-m xg_analysis serve"
        Port        = 8090
    },
    @{
        Name        = "xg-ccc-nginx"
        DisplayName = "XephyGuard CCC Nginx TLS/mTLS Proxy"
        WorkDir     = $NginxDir
        Exe         = $NginxExe
        Args        = "-p `"$NginxDir`""
        Port        = 443
    }
)

function Test-NssmAvailable {
    return $null -ne (Get-Command nssm.exe -ErrorAction SilentlyContinue)
}

function Assert-Elevated {
    if ($IsWindows -or $env:OS -eq "Windows_NT") {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw "Administrator privileges are required to $Action XephyGuard CCC services."
        }
    }
}

function Install-CccServices {
    Assert-Elevated
    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }

    $useNssm = Test-NssmAvailable
    foreach ($svc in $Services) {
        $name = $svc.Name
        $stdoutLog = Join-Path $LogDir "$name.out.log"
        $stderrLog = Join-Path $LogDir "$name.err.log"

        if ($useNssm) {
            Write-Host "[NSSM] Registering Windows service $name ($($svc.DisplayName))..." -ForegroundColor Cyan
            & nssm.exe stop $name 2>$null | Out-Null
            & nssm.exe remove $name confirm 2>$null | Out-Null
            & nssm.exe install $name $svc.Exe $svc.Args
            & nssm.exe set $name DisplayName $svc.DisplayName
            & nssm.exe set $name AppDirectory $svc.WorkDir
            & nssm.exe set $name Start SERVICE_AUTO_START
            & nssm.exe set $name AppStdout $stdoutLog
            & nssm.exe set $name AppStderr $stderrLog
            & nssm.exe set $name AppRotateFiles 1
            & nssm.exe set $name AppRotateBytes 10485760
            & nssm.exe set $name AppExit Default Restart
            & nssm.exe set $name AppRestartDelay 5000
            & sc.exe failure $name reset= 60 actions= restart/5000/restart/5000/restart/10000 | Out-Null
        } else {
            Write-Host "[ScheduledTask] Registering resilient background task $name..." -ForegroundColor Cyan
            $actionObj = New-ScheduledTaskAction -Execute $svc.Exe -Argument $svc.Args -WorkingDirectory $svc.WorkDir
            $triggerStartup = New-ScheduledTaskTrigger -AtStartup
            $settings = New-ScheduledTaskSettingsSet `
                -AllowStartIfOnBatteries `
                -DontStopIfGoingOnBatteries `
                -StartWhenAvailable `
                -RestartCount 999 `
                -RestartInterval (New-TimeSpan -Minutes 1) `
                -ExecutionTimeLimit ([TimeSpan]::Zero) `
                -MultipleInstances IgnoreNew
            Register-ScheduledTask -TaskName $name `
                -Description $svc.DisplayName `
                -Action $actionObj `
                -Trigger $triggerStartup `
                -Settings $settings `
                -User "SYSTEM" `
                -RunLevel Highest `
                -Force | Out-Null
        }
    }
    Write-Host "All XephyGuard CCC services installed with automatic failure recovery." -ForegroundColor Green
}

function Uninstall-CccServices {
    Assert-Elevated
    Stop-CccServices
    $useNssm = Test-NssmAvailable
    foreach ($svc in $Services) {
        $name = $svc.Name
        if ($useNssm -and (Get-Service -Name $name -ErrorAction SilentlyContinue)) {
            & nssm.exe remove $name confirm | Out-Null
            Write-Host "Removed Windows service $name"
        }
        if (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) {
            Unregister-ScheduledTask -TaskName $name -Confirm:$false
            Write-Host "Removed Scheduled Task $name"
        }
    }
}

function Start-CccServices {
    Assert-Elevated
    foreach ($svc in $Services) {
        $name = $svc.Name
        if (Get-Service -Name $name -ErrorAction SilentlyContinue) {
            Start-Service -Name $name
            Write-Host "Started service $name" -ForegroundColor Green
        } elseif (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) {
            Start-ScheduledTask -TaskName $name
            Write-Host "Started scheduled task $name" -ForegroundColor Green
        } else {
            Write-Warning "$name is not installed. Run '.\scripts\windows-ccc-service.ps1 install' first."
        }
    }
}

function Stop-CccServices {
    Assert-Elevated
    foreach ($svc in $Services) {
        $name = $svc.Name
        if (Get-Service -Name $name -ErrorAction SilentlyContinue) {
            Stop-Service -Name $name -Force -ErrorAction SilentlyContinue
            Write-Host "Stopped service $name" -ForegroundColor Yellow
        } elseif (Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue) {
            Stop-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
            Write-Host "Stopped scheduled task $name" -ForegroundColor Yellow
        }
    }
}

function Show-CccStatus {
    $rows = @()
    foreach ($svc in $Services) {
        $name = $svc.Name
        $winSvc = Get-Service -Name $name -ErrorAction SilentlyContinue
        $task = Get-ScheduledTask -TaskName $name -ErrorAction SilentlyContinue
        $mode = "Not Installed"
        $state = "Stopped"
        if ($winSvc) {
            $mode = "WindowsService (NSSM)"
            $state = [string]$winSvc.Status
        } elseif ($task) {
            $mode = "ScheduledTask (SYSTEM)"
            $state = [string]$task.State
        }

        $portListening = "-"
        if ($svc.Port -gt 0) {
            $tcp = Get-NetTCPConnection -LocalPort $svc.Port -State Listen -ErrorAction SilentlyContinue
            $portListening = if ($tcp) { "Listening ($($svc.Port))" } else { "DOWN ($($svc.Port))" }
        }

        $rows += [PSCustomObject]@{
            Service = $name
            Manager = $mode
            State   = $state
            Port    = $portListening
        }
    }
    $rows | Format-Table -AutoSize
}

function Invoke-CccHealth {
    Show-CccStatus
    $healthUrl = "http://127.0.0.1:8000/healthz"
    Write-Host "`nQuerying CCC health endpoint ($healthUrl)..." -ForegroundColor Cyan
    try {
        $resp = Invoke-RestMethod -Uri $healthUrl -Method Get -TimeoutSec 5
        $json = $resp | ConvertTo-Json -Depth 5
        Write-Host $json -ForegroundColor Green
        if ($resp.status -ne "ok") {
            Write-Error "CCC health status reported '$($resp.status)'."
            exit 1
        }
    } catch {
        Write-Error "CCC health check failed at ${healthUrl}: $($_.Exception.Message)"
        exit 1
    }
}

switch ($Action) {
    "install"   { Install-CccServices }
    "uninstall" { Uninstall-CccServices }
    "start"     { Start-CccServices }
    "stop"      { Stop-CccServices }
    "restart"   { Stop-CccServices; Start-Sleep -Seconds 2; Start-CccServices }
    "status"    { Show-CccStatus }
    "health"    { Invoke-CccHealth }
}
