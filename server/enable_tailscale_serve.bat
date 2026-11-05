@echo off
echo ========================================================
echo       FAKE-VR: CONFIGURE TAILSCALE SINGLE-PORT ACCESS
echo ========================================================
echo.

set TAILSCALE="C:\Program Files\Tailscale\tailscale.exe"

if not exist %TAILSCALE% (
    echo [ERROR] Tailscale not found at %TAILSCALE%.
    pause
    exit /b 1
)

echo [1/3] Checking Tailscale status...
%TAILSCALE% status >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Tailscale is currently logged out.
    echo Logging in now... A browser window will open to authenticate.
    %TAILSCALE% up
)

echo.
echo [2/3] Configuring single-port forwarder (Port 7777 ONLY)...
echo Exposing only the Fake-VR TCP server on port 7777.
echo All other files, services, and ports remain completely locked.
%TAILSCALE% serve --bg --tcp 7777 tcp://127.0.0.1:7777

echo.
echo [3/3] Configuration Complete!
for /f "tokens=*" %%i in ('%TAILSCALE% ip -4') do set TS_IP=%%i
echo ========================================================
echo Your Tailscale IP: %TS_IP%
echo Give your friends this address to connect:
echo.
echo    python client\run_client.py --server %TS_IP% --port 7777
echo ========================================================
pause
