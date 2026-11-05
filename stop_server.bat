@echo off
title Fake-VR Server & Tunnel Stopper
echo Stopping Fake-VR server and active tunnels...
taskkill /F /IM python.exe /FI "WINDOWTITLE eq Fake-VR*" 2>nul
taskkill /F /IM bore.exe 2>nul
powershell -Command "Get-NetTCPConnection -LocalPort 7777 -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue }"
echo All Fake-VR server processes and tunnels stopped.
timeout /t 2 >nul
