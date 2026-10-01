@echo off
rem Needs Visual Studio 2022 (C++ workload) + CMake. SDK headers go into .\sdk
rem Usage: build.bat [x64^|x86]   (default: x64)
setlocal
set ARCH=%~1
if "%ARCH%"=="" set ARCH=x64
if /i "%ARCH%"=="x64" (set CMAKE_ARCH=x64) else if /i "%ARCH%"=="x86" (set CMAKE_ARCH=Win32) else (
    echo Unknown architecture "%ARCH%" - use x64 or x86
    exit /b 1
)
cmake -S . -B build-%ARCH% -A %CMAKE_ARCH% -DAIMP_SDK_DIR="%~dp0sdk" || goto :err
cmake --build build-%ARCH% --config Release || goto :err
echo.
echo Done: build-%ARCH%\Release\aimp_discord_rpc.dll
pause
exit /b 0
:err
echo Build failed.
pause
exit /b 1
