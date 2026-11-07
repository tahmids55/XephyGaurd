@echo off
title XephyGuard CCC Server
cd /d "%~dp0"

echo ===================================================
echo [1/5] Syncing Network IP and SSL Certificate...
echo ===================================================
python sync_network.py

echo.
echo ===================================================
echo [2/5] Starting Nginx (Ports 80, 443 HTTPS, 8443 Agent mTLS)...
echo ===================================================
if exist "C:\nginx\nginx.exe" (
    taskkill /F /IM nginx.exe >nul 2>&1
    start "XephyGuard Nginx" /D "C:\nginx" "C:\nginx\nginx.exe"
)

cd server
set "PHP_BIN=C:\Users\taki\AppData\Local\Microsoft\WinGet\Packages\PHP.PHP.8.3_Microsoft.Winget.Source_8wekyb3d8bbwe\php.exe"

if not exist "%PHP_BIN%" (
    echo [ERROR] PHP binary not found at %PHP_BIN%
    pause
    exit /b 1
)

echo [3/5] Starting Web Application Server (Port 8000)...
start "XephyGuard Web (Port 8000)" "%PHP_BIN%" artisan serve --host=0.0.0.0 --port=8000

echo [4/5] Starting Reverb WebSocket Server (Port 8080)...
start "XephyGuard Reverb (Port 8080)" "%PHP_BIN%" artisan reverb:start --host=0.0.0.0 --port=8080

echo [5/5] Starting Queue Worker...
start "XephyGuard Queue Worker" "%PHP_BIN%" artisan queue:work

echo.
echo ===================================================
echo ALL SERVER SERVICES ARE RUNNING!
echo   - HTTPS Dashboard : https://localhost/login (or https://<YOUR_IP>/login)
echo   - HTTP  Dashboard : http://localhost:8000/login
echo   - Agent mTLS API  : Port 8443
echo ===================================================
pause
