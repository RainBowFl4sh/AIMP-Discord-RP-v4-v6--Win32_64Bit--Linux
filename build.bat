@echo off
rem Needs Visual Studio 2022 (C++ workload) + CMake. SDK headers go into .\sdk
cmake -S . -B build -A x64 -DAIMP_SDK_DIR="%~dp0sdk" || goto :err
cmake --build build --config Release || goto :err
echo.
echo Done: build\Release\aimp_discord_rpc.dll
pause
exit /b 0
:err
echo Build failed.
pause
exit /b 1
