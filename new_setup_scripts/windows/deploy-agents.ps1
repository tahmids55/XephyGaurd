# ==============================================================================
# XephyGuard Lab Mass Deployment Script (Windows Student PCs via WinRM)
# Auto-discovers student PCs on the network and installs the Agent in parallel.
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$AutoDiscover = $true,
    [string[]]$IPList = @(),
    [string]$TeacherIP = "",
    [string]$SetupFile = "",
    [string]$SetupUrl = ""
)

$RepoRoot  = (Get-Item (Join-Path $PSScriptRoot "..\..")).FullName
$ServerDir = Join-Path $RepoRoot "server"
. (Join-Path $RepoRoot "start_server\windows\_common.ps1")

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "       XephyGuard Auto-Discovery & Mass Installer         " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

if (-not $TeacherIP) {
    $TeacherIP = Get-XephyLanIp -Fallback "127.0.0.1"
}

$gateway = (Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty NextHop -First 1)
$subnetPrefix = ($TeacherIP -split '\.')[0..2] -join '.'

Write-Host "Teacher CCC IP:  $TeacherIP" -ForegroundColor White
Write-Host "Subnet Detected: $subnetPrefix.0/24 (Gateway: $gateway)" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 2. Target Discovery
# ------------------------------------------------------------------------------
$Targets = @()

if ($IPList.Count -gt 0) {
    $Targets = $IPList
    Write-Host "`n[+] Using manually specified IP list ($($Targets.Count) PCs)." -ForegroundColor Green
} else {
    Write-Host "`n[*] Scanning $subnetPrefix.0/24 for active student computers..." -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $tasks = 1..254 | ForEach-Object {
        $ip = "$subnetPrefix.$_"
        if ($ip -ne $TeacherIP -and $ip -ne $gateway) {
            $p = [System.Net.NetworkInformation.Ping]::new()
            @{ IP = $ip; Task = $p.SendPingAsync($ip, 350) }
        }
    }

    $discoveredIps = @()
    foreach ($t in $tasks) {
        if ($t -and $t.Task.Result.Status -eq "Success") {
            $discoveredIps += $t.IP
        }
    }

    $neighbors = Get-NetNeighbor -AddressFamily IPv4 -ErrorAction SilentlyContinue | Where-Object { $_.State -in "Reachable", "Stale" }
    foreach ($n in $neighbors) {
        $ip = $n.IPAddress
        if ($ip -like "$subnetPrefix.*" -and $ip -ne $TeacherIP -and $ip -ne $gateway -and $ip -notmatch "\.255$" -and $discoveredIps -notcontains $ip) {
            $discoveredIps += $ip
        }
    }

    $sw.Stop()
    Write-Host "[+] Discovery completed in $([math]::Round($sw.Elapsed.TotalSeconds, 1))s.`n" -ForegroundColor Green

    if ($discoveredIps.Count -eq 0) {
        Write-Warning "No active student computers found on $subnetPrefix.0/24."
        $manual = Read-Host "Enter student IP(s) manually (comma-separated, e.g. 192.168.1.50,192.168.1.51) or press Enter to exit"
        if (-not $manual) { exit 0 }
        $Targets = $manual -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
    } else {
        Write-Host "Found $($discoveredIps.Count) active computers on the network:" -ForegroundColor Yellow
        Write-Host "----------------------------------------------------------" -ForegroundColor DarkGray
        $discoveredList = @()
        $idx = 1
        foreach ($ip in $discoveredIps) {
            $mac = ($neighbors | Where-Object { $_.IPAddress -eq $ip } | Select-Object -ExpandProperty LinkLayerAddress -First 1)
            if (-not $mac) { $mac = "Unknown" }
            Write-Host ("  [{0,2}] {1,-16}  MAC: {2}" -f $idx, $ip, $mac) -ForegroundColor White
            $discoveredList += $ip
            $idx++
        }
        Write-Host "----------------------------------------------------------" -ForegroundColor DarkGray
        $confirm = Read-Host "`nAuto-install XephyGuard Agent on ALL $($discoveredList.Count) computers? [Y/n]"
        if ($confirm -and $confirm.Trim().ToLower() -eq "n") {
            Write-Host "Aborted by user." -ForegroundColor Yellow
            exit 0
        }
        $Targets = $discoveredList
    }
}

# ------------------------------------------------------------------------------
# 3. Retrieve Active Lab Credentials & Installer URL
# ------------------------------------------------------------------------------
$CAFP = Get-XephyCaFingerprint -ServerDir $ServerDir
if (-not $CAFP) {
    Write-Error "CA certificate not found in $ServerDir\storage\app\private\ca\ca.crt. Run .\new_setup_scripts\windows\setup-server.ps1 first."
    exit 1
}

$KEY = ""
try {
    $phpExe = Find-XephyPhp -RepoRoot $RepoRoot
    if ($phpExe) {
        $KEY = (& $phpExe "$ServerDir\artisan" xephyguard:enroll-key 2>$null).Trim()
    }
} catch {}

if (-not $KEY) {
    $KEY = Read-Host "Enter Lab Enrollment Key (from Downloads page)"
}

$resolvedSetupUrl = $SetupUrl
if (-not $resolvedSetupUrl) {
    if (-not $SetupFile) {
        try {
            $dlPage = Invoke-WebRequest -Uri "http://$TeacherIP/downloads" -UseBasicParsing -TimeoutSec 5
            if ($dlPage.Content -match '/downloads/(XephyGuard-Setup[^"''\s<>]+\.exe)') {
                $SetupFile = $Matches[1]
            }
        } catch {}
    }
    if (-not $SetupFile) {
        $localDl = Join-Path $ServerDir "storage\app\public\downloads"
        if (Test-Path $localDl) {
            $latest = Get-ChildItem -Path $localDl -Filter "XephyGuard-Setup*.exe" -File |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if ($latest) { $SetupFile = $latest.Name }
        }
    }
    if (-not $SetupFile) {
        Write-Host "[ERROR] Could not resolve XephyGuard-Setup-*.exe automatically. Pass -SetupFile or -SetupUrl." -ForegroundColor Red
        exit 1
    }
    $resolvedSetupUrl = "http://$TeacherIP/downloads/$SetupFile"
}
Write-Host "Installer URL:   $resolvedSetupUrl" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 4. Prompt for Student PC Administrator Credentials & Execute
# ------------------------------------------------------------------------------
Write-Host "`nEnter Administrator credentials for the Student PCs:" -ForegroundColor Yellow
$Cred = Get-Credential

Write-Host "`n[*] Deploying XephyGuard Agent to $($Targets.Count) PC(s)..." -ForegroundColor Cyan

$scriptBlock = {
    param($serverIp, $key, $cafp, $setupUrl)

    $tempExe = Join-Path $env:TEMP "XephyGuard-Setup.exe"
    Invoke-WebRequest -Uri $setupUrl -OutFile $tempExe -UseBasicParsing

    $installArgs = "/S /ENROLLKEY=$key /CCC=$serverIp"
    if ($cafp) { $installArgs += " /CAFP=$cafp" }
    $p = Start-Process -FilePath $tempExe -ArgumentList $installArgs -Wait -PassThru

    Remove-Item $tempExe -Force -ErrorAction SilentlyContinue
    return @{
        ExitCode = $p.ExitCode
        Computer = $env:COMPUTERNAME
    }
}

try {
    $thPath = "WSMan:\localhost\Client\TrustedHosts"
    $cur = ((Get-Item $thPath -ErrorAction SilentlyContinue).Value -split ',') |
        ForEach-Object { $_.Trim() } | Where-Object { $_ }
    if ($cur -notcontains "*") {
        $merged = @($cur + $Targets) | Select-Object -Unique
        if ($merged.Count -gt 0) {
            Set-Item $thPath -Value ($merged -join ',') -Force -ErrorAction Stop
        }
    }
} catch {}

$results = Invoke-Command -ComputerName $Targets -Credential $Cred -ScriptBlock $scriptBlock -ArgumentList $TeacherIP, $KEY, $CAFP, $resolvedSetupUrl -ErrorVariable failedRemotes -ErrorAction SilentlyContinue

Write-Host "`n==================== DEPLOYMENT SUMMARY ====================" -ForegroundColor Cyan
foreach ($r in $results) {
    if ($r.ExitCode -eq 0) {
        Write-Host ("  [OK]   {0,-16} -> Successfully installed & enrolled ({1})" -f $r.PSComputerName, $r.Computer) -ForegroundColor Green
    } else {
        Write-Host ("  [WARN] {0,-16} -> Exit code {1}" -f $r.PSComputerName, $r.ExitCode) -ForegroundColor Yellow
    }
}
foreach ($fail in $failedRemotes) {
    Write-Host ("  [FAIL] {0,-16} -> {1}" -f $fail.TargetObject, $fail.Exception.Message) -ForegroundColor Red
}
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "`n[OK] Finished. Open Dashboard at https://$TeacherIP/ to view connected PCs." -ForegroundColor Green
