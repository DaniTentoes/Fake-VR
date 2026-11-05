@echo off
setlocal
echo ========================================================
echo        BUILDING FAKE-VR NATIVE C++ CLIENT (MSVC x64)
echo ========================================================

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist bin mkdir bin

cl.exe /nologo /O2 /W3 /std:c++17 /EHsc /MD ^
    /I client_cpp\include ^
    /I steam_audio_sdk\steamaudio\include ^
    client_cpp\src\main.cpp ^
    /link /LIBPATH:client_cpp\lib phonon.lib ole32.lib oleaut32.lib user32.lib advapi32.lib ^
    /OUT:bin\FakeVR_Client.exe

if %errorlevel% equ 0 (
    echo.
    echo ========================================================
    echo Build SUCCESS! Copying required DLLs...
    copy /y client_cpp\lib\*.dll bin\ >nul
    echo Executable ready at bin\FakeVR_Client.exe
    echo ========================================================
) else (
    echo.
    echo [ERROR] Build failed.
)
