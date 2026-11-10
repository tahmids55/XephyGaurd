# ==============================================================================
# XephyGuard Lab Mass Deployment Script
# Auto-discovers student PCs on the network and installs the Agent in parallel.
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$AutoDiscover = $true,
    [string[]]$IPList = @(),
    [string]$TeacherIP = ""
)

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "       XephyGuard Auto-Discovery & Mass Installer         " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# ------------------------------------------------------------------------------
# 1. Resolve Teacher IP & Subnet
# ------------------------------------------------------------------------------
if (-not $TeacherIP) {
    $activeIp = Get-NetIPAddress -AddressFamily IPv4 | 
        Where-Object { 
            $_.InterfaceAlias -notmatch "Loopback|vEthernet|WSL" -and 
            $_.IPAddress -notmatch "^169\.254|^127\." 
        } | Select-Object -ExpandProperty IPAddress -First 1
    $TeacherIP = if ($activeIp) { $activeIp } else { "127.0.0.1" }
}

$gateway = (Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction SilentlyContinue | Select-Object -ExpandProperty NextHop -First 1)
$subnetPrefix = ($TeacherIP -split '\.')[0..2] -join '.'

Write-Host "Teacher CCC IP:  $TeacherIP" -ForegroundColor White
Write-Host "Subnet Detected: $subnetPrefix.0/24 (Gateway: $gateway)" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 2. Auto-Discovery Engine (Terminal Ping Sweep)
# ------------------------------------------------------------------------------
$Targets = @()

if ($IPList.Count -gt 0) {
    $Targets = $IPList
    Write-Host "`n[+] Using manually specified IP list ($($Targets.Count) PCs)." -ForegroundColor Green
} else {
    Write-Host "`n[*] Scanning $subnetPrefix.0/24 for active student computers..." -ForegroundColor Cyan
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    
    # Fast multi-threaded async ping across all 254 subnet hosts
    $tasks = 1..254 | ForEach-Object {
        $ip = "$subnetPrefix.$_"
        if ($ip -ne $TeacherIP -and $ip -ne $gateway) {
            $p = [System.Net.NetworkInformation.Ping]::new()
            @{ IP = $ip; Task = $p.SendPingAsync($ip, 350) }
        }
    }
    
    # Collect successful responses
    $discoveredIps = @()
    foreach ($item in $tasks) {
        try {
            if ($item.Task.Result.Status -eq "Success") {
                $discoveredIps += $item.IP
            }
        } catch {}
    }
    
    # Cross-reference with ARP neighbor table for MAC addresses
    $neighbors = Get-NetNeighbor -AddressFamily IPv4 -ErrorAction SilentlyContinue | 
        Where-Object { $_.IPAddress -match "^$([regex]::Escape($subnetPrefix))\." -and $_.State -in "Reachable","Permanent" }
    
    foreach ($n in $neighbors) {
        $ip = $n.IPAddress
        if ($ip -ne $TeacherIP -and $ip -ne $gateway -and $ip -notmatch "\.255$" -and $discoveredIps -notcontains $ip) {
            $discoveredIps += $ip
        }
    }
    
    $discoveredIps = $discoveredIps | Sort-Object { [version]$_ }
    $sw.Stop()
    
    Write-Host "[✔] Discovery completed in $([math]::Round($sw.Elapsed.TotalSeconds, 2)) seconds!`n" -ForegroundColor Green
    
    if ($discoveredIps.Count -eq 0) {
        Write-Warning "No other computers responded on $subnetPrefix.0/24."
        Write-Host "Make sure student PCs are powered on and connected to the same switch/Wi-Fi."
        exit 0
    }
    
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

# ------------------------------------------------------------------------------
# 3. Retrieve Active Lab Credentials from Server
# ------------------------------------------------------------------------------
$RepoRoot = (Get-Item $PSScriptRoot).FullName
$ServerDir = Join-Path $RepoRoot "server"
$caCertPath = Join-Path $ServerDir "storage\app\private\ca\ca.crt"

if (-not (Test-Path $caCertPath)) {
    Write-Error "CA certificate not found in $caCertPath. Please run run-native.ps1 first."
    exit 1
}

$rawCert = Get-Content $caCertPath -Raw
$certBytes = [System.Convert]::FromBase64String(($rawCert -replace '-----.*?-----|\r|\n', ''))
$hasher = [System.Security.Cryptography.SHA256]::Create()
$CAFP = (-join ($hasher.ComputeHash($certBytes) | ForEach-Object { $_.ToString("x2") })).ToLower()

$KEY = ""
try {
    $phpExe = (Get-Command php.exe -ErrorAction SilentlyContinue).Source
    if ($phpExe) {
        $KEY = (& $phpExe "$ServerDir\artisan" xephyguard:enroll-key 2>$null).Trim()
    }
} catch {}

if (-not $KEY) {
    $KEY = Read-Host "Enter Lab Enrollment Key (from Downloads page)"
}

# ------------------------------------------------------------------------------
# 4. Prompt for Student PC Administrator Credentials
# ------------------------------------------------------------------------------
Write-Host "`nEnter Student PC Administrator Credentials:" -ForegroundColor Cyan
Write-Host "(Username and password provided by your Lab Assistant)" -ForegroundColor Gray
$Cred = Get-Credential

# ------------------------------------------------------------------------------
# 5. Remote Parallel Deployment
# ------------------------------------------------------------------------------
Write-Host "`n[+] Deploying to $($Targets.Count) student PCs in parallel..." -ForegroundColor Green

$scriptBlock = {
    param($serverIp, $key, $cafp)
    
    $setupUrl = "http://$serverIp/downloads/XephyGuard-Setup-0.1.0.exe"
    $tempExe = "$env:TEMP\XephyGuard-Setup.exe"
    
    # Download installer directly from Teacher PC
    Invoke-WebRequest -Uri $setupUrl -OutFile $tempExe -UseBasicParsing
    
    # Execute silent install
    $args = "/S /KEY=$key /CCC=$serverIp /CAFP=$cafp"
    $p = Start-Process -FilePath $tempExe -ArgumentList $args -Wait -PassThru
    
    Remove-Item $tempExe -Force -ErrorAction SilentlyContinue
    
    return @{
        ExitCode = $p.ExitCode
        Computer = $env:COMPUTERNAME
    }
}

try {
    Set-Item WSMan:\localhost\Client\TrustedHosts -Value "*" -Force -ErrorAction SilentlyContinue
} catch {}

$results = Invoke-Command -ComputerName $Targets -Credential $Cred -ScriptBlock $scriptBlock -ArgumentList $TeacherIP, $KEY, $CAFP -ErrorVariable failedRemotes -ErrorAction SilentlyContinue

# ------------------------------------------------------------------------------
# 6. Deployment Summary
# ------------------------------------------------------------------------------
Write-Host "`n==========================================================" -ForegroundColor Cyan
Write-Host "                 Deployment Summary                       " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

foreach ($res in $results) {
    if ($res.ExitCode -eq 0) {
        Write-Host "  [OK] $($res.PSComputerName) ($($res.Computer)): Installed & Enrolled successfully!" -ForegroundColor Green
    } else {
        Write-Host "  [!]  $($res.PSComputerName): Installer returned exit code $($res.ExitCode)" -ForegroundColor Yellow
    }
}

if ($failedRemotes) {
    Write-Host "`nFailed to reach the following PCs (Offline or Remote Access blocked):" -ForegroundColor Red
    foreach ($err in $failedRemotes) {
        Write-Host "  - $($err.TargetObject): $($err.Exception.Message)" -ForegroundColor Red
    }
}

Write-Host "`n[✔] Finished! Open Dashboard at https://$TeacherIP/ to view your live classroom." -ForegroundColor Green
