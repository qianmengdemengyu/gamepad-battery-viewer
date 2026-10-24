@echo off
rem Build with MSVC. Run from "x64 Native Tools Command Prompt for VS".
setlocal

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe not found.
    echo Open "x64 Native Tools Command Prompt for VS" and run this script again.
    exit /b 1
)

if not exist build mkdir build

cl /nologo /W3 /EHsc /utf-8 /DUNICODE /D_UNICODE /Fo:build\ /Fe:build\gamepad-battery.exe src\gamepad-battery.cpp user32.lib gdi32.lib shell32.lib gdiplus.lib
if errorlevel 1 (
    echo [ERROR] build failed.
    exit /b 1
)

echo.
echo [OK] build\gamepad-battery.exe
endlocal
