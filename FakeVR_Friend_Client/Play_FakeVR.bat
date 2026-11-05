@echo off
title Fake-VR Launcher
cd /d "%~dp0"
if exist "bin\FakeVR_Launcher.exe" (
    bin\FakeVR_Launcher.exe %*
) else (
    bin\FakeVR_Client.exe %*
)
