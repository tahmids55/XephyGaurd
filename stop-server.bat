@echo off
title XephyGuard Server Terminator
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0stop-server.ps1"
pause
