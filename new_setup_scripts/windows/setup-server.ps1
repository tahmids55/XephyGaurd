# ==============================================================================
# XephyGuard Native CCC Server First-Time Setup (Windows 10/11 / Server)
# Configures PHP 8.3+, Composer/Vite dependencies, Nginx reverse proxy,
# .env secrets, SQLite DB, Lab CA + Server TLS certs, Firewall, & launches.
# ==============================================================================

[CmdletBinding()]
param(
    [string]$ServerIp = "",
    [int]$AppPort = 0,
    [int]$ReverbPort = 0,
    [int]$GatewayPort = 0,
    [int]$HttpPort = 80,
    [int]$HttpsPort = 443,
    [int]$MtlsPort = 8443,
    [string]$NginxDir = $env:NGINX_DIR,
    [string]$NginxVersion = $(if ($env:NGINX_VERSION) { $env:NGINX_VERSION } else { "1.26.2" }),
    [switch]$ForceDownload = $false,
    [switch]$EnableAutoStart = $false,
    [switch]$DisableAutoStart = $false,
    [switch]$SkipStart = $false
)

$RepoRoot  = (Get-Item (Join-Path $PSScriptRoot "..\..")).FullName
$ServerDir = Join-Path $RepoRoot "server"
$ToolsDir  = Join-Path $RepoRoot "tools"
. (Join-Path $RepoRoot "start_server\windows\_common.ps1")

# Self-elevate interactively for Firewall / Nginx setup
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    if ([Environment]::UserInteractive -and -not [Console]::IsInputRedirected) {
        Write-Warning "Requesting Administrator privileges..."
        try {
            $argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
            if ($ServerIp)         { $argList += " -ServerIp `"$ServerIp`"" }
            if ($AppPort -gt 0)    { $argList += " -AppPort $AppPort" }
            if ($ReverbPort -gt 0) { $argList += " -ReverbPort $ReverbPort" }
            if ($GatewayPort -gt 0){ $argList += " -GatewayPort $GatewayPort" }
            if ($HttpPort -ne 80)  { $argList += " -HttpPort $HttpPort" }
            if ($HttpsPort -ne 443){ $argList += " -HttpsPort $HttpsPort" }
            if ($MtlsPort -ne 8443){ $argList += " -MtlsPort $MtlsPort" }
            if ($NginxDir)         { $argList += " -NginxDir `"$NginxDir`"" }
            if ($ForceDownload)    { $argList += " -ForceDownload" }
            if ($EnableAutoStart)  { $argList += " -EnableAutoStart" }
            if ($DisableAutoStart) { $argList += " -DisableAutoStart" }
            if ($SkipStart)        { $argList += " -SkipStart" }
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

$NginxDir = Find-XephyNginx -RepoRoot $RepoRoot -PreferredDir $NginxDir

# ------------------------------------------------------------------------------
# Auto-Start Management
# ------------------------------------------------------------------------------
$AutoStartTaskName = "XephyGuardServer"
$StartupShortcut = Join-Path "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup" "XephyGuard-Server.lnk"

if ($DisableAutoStart) {
    if (Test-Path $StartupShortcut) { Remove-Item $StartupShortcut -Force }
    Unregister-ScheduledTask -TaskName $AutoStartTaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "[OK] Windows Auto-Start has been DISABLED." -ForegroundColor Yellow
    exit 0
}

if ($EnableAutoStart) {
    try {
        $wsh = New-Object -ComObject WScript.Shell
        $sc = $wsh.CreateShortcut($StartupShortcut)
        $sc.TargetPath = "powershell.exe"
        $sc.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSCommandPath`""
        $sc.WorkingDirectory = $RepoRoot
        $sc.Description = "XephyGuard Native Server Auto-Start"
        $sc.Save()
        Write-Host "[OK] Windows Auto-Start has been ENABLED!" -ForegroundColor Green
    } catch {
        Write-Warning "Could not configure Auto-Start shortcut: $_"
    }
}

Write-Host ""
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "          XephyGuard First-Time Setup (Windows)           " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "Repository root: $RepoRoot" -ForegroundColor Gray

# ------------------------------------------------------------------------------
# 1. Detect Host LAN / Wi-Fi IP
# ------------------------------------------------------------------------------
if (-not $ServerIp) {
    $ServerIp = Get-XephyLanIp -Fallback "127.0.0.1"
    Write-Host "[+] Detected LAN Address: $ServerIp" -ForegroundColor Green
} else {
    Write-Host "[+] Using specified Server Address: $ServerIp" -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 2. Check & Install PHP (Requires >= 8.3)
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 1/6] Checking PHP (Requires PHP 8.3+) ---" -ForegroundColor Yellow

$PhpExe = if ($ForceDownload) { $null } else { Find-XephyPhp -RepoRoot $RepoRoot }
if ($PhpExe) {
    Write-Host "[OK] Using PHP: $PhpExe" -ForegroundColor Green
} else {
    Write-Host "[*] No compatible PHP 8.3+ found. Installing PHP 8.3 via winget..." -ForegroundColor Cyan
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($winget) {
        & winget.exe install --id PHP.PHP.8.3 --exact --source winget --accept-source-agreements --accept-package-agreements --silent
        $PhpExe = Find-XephyPhp -RepoRoot $RepoRoot
    }
    if (-not $PhpExe) {
        Write-Error "Could not find or install PHP 8.3+. Install PHP 8.3+ or place php.exe in $ToolsDir\php\."
        exit 1
    }
}

$phpIniPath = Join-Path (Split-Path $PhpExe) "php.ini"
if (-not (Test-Path $phpIniPath)) {
    $prodIni = Join-Path (Split-Path $PhpExe) "php.ini-production"
    if (Test-Path $prodIni) { Copy-Item $prodIni $phpIniPath }
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
        Write-Host "[+] Enabled required PHP extensions in php.ini." -ForegroundColor Gray
    }
}

# ------------------------------------------------------------------------------
# 3. Ensure Composer Dependencies & Frontend Assets
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 2/6] Checking PHP & Frontend Dependencies ---" -ForegroundColor Yellow

$vendorAutoload = Join-Path $ServerDir "vendor\autoload.php"
if (-not (Test-Path $vendorAutoload)) {
    Write-Host "[*] Installing Composer dependencies in $ServerDir..." -ForegroundColor Cyan
    $composerCmd = (Get-Command composer.bat -ErrorAction SilentlyContinue).Source
    if (-not $composerCmd) { $composerCmd = (Get-Command composer -ErrorAction SilentlyContinue).Source }
    Push-Location $ServerDir
    try {
        if ($composerCmd) {
            & $composerCmd install --no-interaction --prefer-dist
        } else {
            New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null
            $composerPhar = Join-Path $ToolsDir "composer.phar"
            if (-not (Test-Path $composerPhar)) {
                Invoke-WebRequest -Uri "https://getcomposer.org/composer-stable.phar" -OutFile $composerPhar -UseBasicParsing
            }
            & $PhpExe $composerPhar install --no-interaction --prefer-dist
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[OK] Composer packages already installed." -ForegroundColor Green
}

$viteManifest = Join-Path $ServerDir "public\build\manifest.json"
if (-not (Test-Path $viteManifest)) {
    $npmCmd = (Get-Command npm.cmd -ErrorAction SilentlyContinue).Source
    if (-not $npmCmd) { $npmCmd = (Get-Command npm -ErrorAction SilentlyContinue).Source }
    if ($npmCmd) {
        Write-Host "[*] Building frontend assets via npm..." -ForegroundColor Cyan
        Push-Location $ServerDir
        try {
            if (Test-Path "package-lock.json") { & $npmCmd ci --no-audit --no-fund } else { & $npmCmd install --no-audit --no-fund }
            & $npmCmd run build
        } finally {
            Pop-Location
        }
    } else {
        Write-Warning "Frontend build missing ($viteManifest) and npm not found. Install Node.js 20+ and run 'npm ci && npm run build' inside server\."
    }
} else {
    Write-Host "[OK] Frontend build manifest present." -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 4. Check & Install Nginx for Windows
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 3/6] Checking Nginx ---" -ForegroundColor Yellow

$NginxExe = Join-Path $NginxDir "nginx.exe"
if (-not (Test-Path $NginxExe) -or $ForceDownload) {
    $NginxDir = Join-Path $ToolsDir "nginx"
    $NginxExe = Join-Path $NginxDir "nginx.exe"
    Write-Host "[*] Downloading Nginx $NginxVersion to $NginxDir..." -ForegroundColor Cyan

    $nginxZipUrl = "https://nginx.org/download/nginx-$NginxVersion.zip"
    $tempZip     = Join-Path $env:TEMP "nginx-$NginxVersion.zip"
    $tempExtract = Join-Path $env:TEMP "nginx-extract"

    Get-Process nginx -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Invoke-WebRequest -Uri $nginxZipUrl -OutFile $tempZip -UseBasicParsing

    if (Test-Path $tempExtract) { Remove-Item $tempExtract -Recurse -Force }
    Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force

    New-Item -ItemType Directory -Path $ToolsDir -Force | Out-Null
    if (Test-Path $NginxDir) { Remove-Item $NginxDir -Recurse -Force }
    Move-Item -Path (Join-Path $tempExtract "nginx-$NginxVersion") -Destination $NginxDir

    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host "[OK] Nginx installed to $NginxDir" -ForegroundColor Green
} else {
    Write-Host "[OK] Nginx installed: $NginxExe" -ForegroundColor Green
}

# ------------------------------------------------------------------------------
# 5. Configure Application Environment, Database & Certificates
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 4/6] Configuring XephyGuard Environment, DB & CA ---" -ForegroundColor Yellow

$EnvFile = Join-Path $ServerDir ".env"
$ianaTz = "UTC"
try {
    $outTz = ""
    if ([System.TimeZoneInfo].GetMethod("TryConvertWindowsIdToIanaId", [type[]]@([string], [string].MakeByRefType()))) {
        if ([System.TimeZoneInfo]::TryConvertWindowsIdToIanaId([System.TimeZoneInfo]::Local.Id, [ref]$outTz) -and $outTz) {
            $ianaTz = $outTz
        }
    }
} catch {}

if (-not (Test-Path $EnvFile)) {
    Write-Host "[*] Creating fresh .env file..." -ForegroundColor Cyan
    Copy-Item (Join-Path $ServerDir ".env.example") $EnvFile

    $revKey = New-RandomHex 12
    $revSec = New-RandomHex 24
    $envRaw = Get-Content $EnvFile -Raw
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "APP_URL" -Value "https://$ServerIp"
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "APP_TIMEZONE" -Value $ianaTz
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "DB_CONNECTION" -Value "sqlite"
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "BROADCAST_CONNECTION" -Value "reverb"
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "REVERB_APP_KEY" -Value $revKey
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "REVERB_APP_SECRET" -Value $revSec
    Set-Content -Path $EnvFile -Value $envRaw -NoNewline

    & $PhpExe "$ServerDir\artisan" key:generate --force
}

$envMap = Read-DotEnvMap -Path $EnvFile
if ($AppPort -le 0)     { $AppPort     = if ($envMap["APP_PORT"] -match '^\d+$')            { [int]$envMap["APP_PORT"] }            else { 8000 } }
if ($ReverbPort -le 0)  { $ReverbPort  = if ($envMap["REVERB_PORT"] -match '^\d+$')         { [int]$envMap["REVERB_PORT"] }         else { 8080 } }
if ($GatewayPort -le 0) { $GatewayPort = if ($envMap["SCREEN_GATEWAY_PORT"] -match '^\d+$') { [int]$envMap["SCREEN_GATEWAY_PORT"] } else { 8082 } }

$desiredAppUrl = if ($HttpsPort -eq 443) { "https://$ServerIp" } else { "https://${ServerIp}:$HttpsPort" }
$envRaw = Get-Content $EnvFile -Raw
$envChanged = $false

if ($envMap["APP_URL"] -ne $desiredAppUrl) {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "APP_URL" -Value $desiredAppUrl
    $envChanged = $true
}
if ($envMap["REVERB_PORT"] -ne "$ReverbPort") {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "REVERB_PORT" -Value "$ReverbPort"
    $envChanged = $true
}
if ($envMap["REVERB_SERVER_PORT"] -ne "$ReverbPort") {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "REVERB_SERVER_PORT" -Value "$ReverbPort"
    $envChanged = $true
}
if ($envMap["SCREEN_GATEWAY_PORT"] -ne "$GatewayPort") {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_PORT" -Value "$GatewayPort"
    $envChanged = $true
}
$gwSecret = $envMap["SCREEN_GATEWAY_SECRET"]
if (-not $gwSecret -or $gwSecret.StartsWith("dev-secret")) {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_SECRET" -Value (New-RandomHex 32)
    $envChanged = $true
}
$gwAgentSecret = $envMap["SCREEN_GATEWAY_AGENT_SECRET"]
if (-not $gwAgentSecret -or $gwAgentSecret.StartsWith("dev-secret")) {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_AGENT_SECRET" -Value (New-RandomHex 32)
    $envChanged = $true
}
if ($envMap["SCREEN_GATEWAY_URL"] -eq "ws://127.0.0.1:8082") {
    $envRaw = Set-DotEnvKey -Content $envRaw -Key "SCREEN_GATEWAY_URL" -Value ""
    $envChanged = $true
}
if ($envChanged) {
    Set-Content -Path $EnvFile -Value $envRaw -NoNewline
}

$sqlitePath = Join-Path $ServerDir "database\database.sqlite"
if (-not (Test-Path $sqlitePath)) {
    Write-Host "[*] Creating SQLite database ($sqlitePath)..." -ForegroundColor Cyan
    New-Item -ItemType File -Path $sqlitePath -Force | Out-Null
}
& $PhpExe "$ServerDir\artisan" migrate --force

$caArgs = @("$ServerDir\artisan", "xephyguard:ca:init")
$parsedIp = $null
if ([System.Net.IPAddress]::TryParse($ServerIp, [ref]$parsedIp)) {
    $caArgs += "--server-ip=$ServerIp"
} else {
    $caArgs += "--server-ip=127.0.0.1"
    if ($ServerIp) { $caArgs += "--server-dns=$ServerIp" }
}
$caArgs += "--server-dns=localhost"
$hostDns = if ($env:COMPUTERNAME) { $env:COMPUTERNAME.ToLowerInvariant() } else { "" }
if ($hostDns -and $hostDns -ne "localhost" -and $hostDns -ne $ServerIp) {
    $caArgs += "--server-dns=$hostDns"
}

$caCertPath = Join-Path $ServerDir "storage\app\private\ca\server.crt"
if (-not (Test-Path $caCertPath)) {
    Write-Host "[*] Generating Lab Root CA and Server TLS Certificate..." -ForegroundColor Cyan
    & $PhpExe ($caArgs + "--force")
    & $PhpExe "$ServerDir\artisan" xephyguard:bootstrap
} else {
    Write-Host "[OK] CA exists; ensuring server certificate covers $ServerIp..." -ForegroundColor Green
    & $PhpExe $caArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[X] Certificate re-issue failed." -ForegroundColor Red
        exit 1
    }
    & $PhpExe "$ServerDir\artisan" xephyguard:bootstrap
}

# ------------------------------------------------------------------------------
# 6. Configure Nginx & Firewall
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 5/6] Configuring Nginx & Firewall ---" -ForegroundColor Yellow

$caDirFwd   = (Join-Path $ServerDir "storage\app\private\ca") -replace '\\', '/'
$publicFwd  = (Join-Path $ServerDir "public") -replace '\\', '/'
$confTarget = Join-Path $NginxDir "conf\xephyguard.conf"

$exampleConfPath = Join-Path $RepoRoot "ccc-deploy\nginx\dev-native.conf.example"
if (Test-Path $exampleConfPath) {
    $confText = Get-Content $exampleConfPath -Raw
    $confText = $confText.Replace("@PUBLIC@", $publicFwd).Replace("@CA_DIR@", $caDirFwd)
    $confText = $confText -replace "127\.0\.0\.1:8000", "127.0.0.1:$AppPort"
    $confText = $confText -replace "127\.0\.0\.1:808[01]", "127.0.0.1:$ReverbPort"
    $confText = $confText -replace "127\.0\.0\.1:8082", "127.0.0.1:$GatewayPort"
    $confText = $confText -replace "listen 80;", "listen $HttpPort;"
    $confText = $confText -replace "listen 443 ssl;", "listen $HttpsPort ssl;"
    $confText = $confText -replace "listen 8443 ssl;", "listen $MtlsPort ssl;"
    if ($HttpsPort -ne 443) {
        $confText = $confText -replace 'https://\$host\$request_uri;', "https://`${host}:$HttpsPort`$request_uri;"
    }
    Set-Content -Path $confTarget -Value $confText
}

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
}

$prevEap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
Push-Location $NginxDir
$nginxTest = & $NginxExe -t 2>&1 | Out-String
Pop-Location
$ErrorActionPreference = $prevEap
if ($nginxTest -notmatch "test is successful") {
    Write-Host "[X] Nginx configuration test FAILED:`n$nginxTest" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Nginx configuration test passed." -ForegroundColor Green

try {
    $ruleName = "XephyGuard Native Server"
    $fwPorts = @("$HttpPort", "$HttpsPort", "$GatewayPort", "$MtlsPort")
    $fw = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
    if (-not $fw) {
        New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Protocol TCP -LocalPort $fwPorts -Action Allow -ErrorAction Stop | Out-Null
    } else {
        Set-NetFirewallRule -DisplayName $ruleName -LocalPort $fwPorts -ErrorAction SilentlyContinue
    }
    Write-Host "[+] Windows Firewall configured for ports: $($fwPorts -join ', ')" -ForegroundColor Gray
} catch {
    Write-Host "[!] Skipping Windows Firewall rule (requires Administrator)." -ForegroundColor Gray
}

# ------------------------------------------------------------------------------
# 7. Launch Services
# ------------------------------------------------------------------------------
Write-Host "`n--- [Step 6/6] Starting XephyGuard Services ---" -ForegroundColor Yellow
if ($SkipStart) {
    Write-Host "[OK] Setup completed (-SkipStart supplied)." -ForegroundColor Green
    Write-Host "     Start later via: .\start_server\windows\start-server.ps1" -ForegroundColor Cyan
} else {
    & (Join-Path $RepoRoot "start_server\windows\start-server.ps1") `
        -Restart `
        -AppPort $AppPort `
        -ReverbPort $ReverbPort `
        -GatewayPort $GatewayPort `
        -HttpPort $HttpPort `
        -HttpsPort $HttpsPort `
        -MtlsPort $MtlsPort `
        -NginxDir $NginxDir
}
