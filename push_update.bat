@echo off
title Publish Fake-VR Update
cd /d "%~dp0"
set /p VER="Enter new version number (e.g. 1.0.1): "
if "%VER%"=="" (
    echo Version cannot be empty.
    pause
    exit /b
)
set /p NOTES="Enter brief changelog: "
if "%NOTES%"=="" set NOTES=Performance enhancements and gameplay fixes.

powershell -ExecutionPolicy Bypass -File "publish_update.ps1" -Version "%VER%" -Changelog "%NOTES%"
pause
