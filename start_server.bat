@echo off
title Fake-VR Multiplayer Host Server
cd /d "%~dp0"
echo ========================================================
echo        Starting Fake-VR Multiplayer Server
echo ========================================================
echo.
python server\run_server.py
pause
