# Shared helpers for XephyGuard Windows scripts (ASCII-safe; compatible with PowerShell 5.1+ & 7+).

function Get-XephyRepoRoot {
    return (Get-Item (Join-Path $PSScriptRoot "..\..")).FullName
}

function New-RandomHex {
    param([int]$Bytes = 32)
    $buf = New-Object byte[] $Bytes
    $rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $rng.GetBytes($buf)
    } finally {
        if ($rng -is [IDisposable]) { $rng.Dispose() }
    }
    return ([BitConverter]::ToString($buf) -replace '-', '').ToLowerInvariant()
}

function Read-DotEnvMap {
    param([string]$Path)
    $map = @{}
    if ($Path -and (Test-Path $Path)) {
        foreach ($line in Get-Content $Path) {
            if ($line -match '^\s*([^#\s=]+)\s*=\s*(.*)$') {
                $k = $Matches[1].Trim()
                $v = $Matches[2].Trim()
                if (($v.StartsWith('"') -and $v.EndsWith('"')) -or ($v.StartsWith("'") -and $v.EndsWith("'"))) {
                    if ($v.Length -ge 2) { $v = $v.Substring(1, $v.Length - 2) }
                }
                $map[$k] = $v
            }
        }
    }
    return $map
}

function Set-DotEnvKey {
    param([string]$Content, [string]$Key, [string]$Value)
    $escaped = [regex]::Escape($Key)
    if ($Content -match "(?m)^$escaped=") {
        return ([regex]::Replace($Content, "(?m)^$escaped=.*$", "$Key=$Value"))
    }
    if ($Content -and -not $Content.EndsWith("`n")) { $Content += "`r`n" }
    return "$Content$Key=$Value`r`n"
}

function Test-XephyPhpVersion {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $null }
    try {
        $verId = & $Path -r "echo PHP_VERSION_ID;" 2>$null
        $verStr = & $Path -r "echo PHP_VERSION;" 2>$null
        if ($verId -and [int]$verId -ge 80300) {
            return @{ Path = $Path; Version = $verStr; Valid = $true }
        }
        return @{ Path = $Path; Version = $verStr; Valid = $false }
    } catch {
        return $null
    }
}

function Find-XephyPhp {
    param([string]$RepoRoot)
    $toolsPhp = Join-Path $RepoRoot "tools\php\php.exe"
    $check = Test-XephyPhpVersion $toolsPhp
    if ($check -and $check.Valid) { return $check.Path }

    $candidates = @()
    $cmdPhp = (Get-Command php.exe -ErrorAction SilentlyContinue).Source
    if ($cmdPhp) { $candidates += $cmdPhp }
    $wingetPhps = Get-ChildItem -Path "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Filter "php.exe" -Recurse -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName
    if ($wingetPhps) { $candidates += $wingetPhps }

    foreach ($cand in ($candidates | Select-Object -Unique)) {
        $c = Test-XephyPhpVersion $cand
        if ($c -and $c.Valid) { return $c.Path }
    }
    return $null
}

function Find-XephyNginx {
    param([string]$RepoRoot, [string]$PreferredDir = "")
    if ($PreferredDir) {
        return $PreferredDir
    }
    if ($env:NGINX_DIR) {
        return $env:NGINX_DIR
    }
    $sysNginx = Join-Path $env:SystemDrive "nginx"
    if (Test-Path (Join-Path $sysNginx "nginx.exe")) {
        return $sysNginx
    }
    $nginxOnPath = Get-Command nginx.exe -ErrorAction SilentlyContinue
    if ($nginxOnPath) {
        return (Split-Path $nginxOnPath.Source -Parent)
    }
    return (Join-Path $RepoRoot "tools\nginx")
}

function Get-XephyLanIp {
    param([string]$Fallback = "127.0.0.1")
    $virtualPattern = "VMware|VirtualBox|Hyper-V|vEthernet|WSL|Loopback|TAP|VPN|Bluetooth|Cloudflare|WARP|Tailscale|WireGuard"
    try {
        $activeIp = Get-NetIPConfiguration |
            Where-Object {
                $_.NetAdapter.Status -eq "Up" -and
                $_.InterfaceAlias -notmatch $virtualPattern -and
                $_.IPv4DefaultGateway -ne $null
            } |
            ForEach-Object { $_.IPv4Address } |
            Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
            Select-Object -ExpandProperty IPAddress -First 1
        if ($activeIp) { return $activeIp }
    } catch {}

    try {
        $ipCandidate = Get-NetIPAddress -AddressFamily IPv4 |
            Where-Object {
                $_.InterfaceAlias -notmatch $virtualPattern -and
                $_.IPAddress -notlike "127.*" -and
                $_.IPAddress -notlike "169.254.*"
            } |
            Select-Object -ExpandProperty IPAddress -First 1
        if ($ipCandidate) { return $ipCandidate }
    } catch {}

    return $Fallback
}

function Get-XephyCaFingerprint {
    param([string]$ServerDir)
    $caCertPath = Join-Path $ServerDir "storage\app\private\ca\ca.crt"
    if (-not (Test-Path $caCertPath)) { return "" }
    $rawCert = Get-Content $caCertPath -Raw
    $b64 = ($rawCert -replace '-----.*?-----|\r|\n', '').Trim()
    if (-not $b64) { return "" }
    $certBytes = [System.Convert]::FromBase64String($b64)
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        return (-join ($hasher.ComputeHash($certBytes) | ForEach-Object { $_.ToString("x2") })).ToLowerInvariant()
    } finally {
        if ($hasher -is [IDisposable]) { $hasher.Dispose() }
    }
}
