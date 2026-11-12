@echo off
title XephyGuard First-Time Server Setup
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-server.ps1" %*
pause
