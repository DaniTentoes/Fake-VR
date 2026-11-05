@echo off
title Fake-VR Public Tunnel
cd /d "%~dp0"
echo Starting public tunnel to bore.pub:32827...
:loop
bin\bore.exe local 7777 --to bore.pub --port 32827
timeout /t 2 >nul
goto loop
