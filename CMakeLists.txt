cmake_minimum_required(VERSION 3.20)
project(aimp_discord_rpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static runtime -> no VC++ redistributable needed on the user's PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Folder that contains apiPlugin.h, apiCore.h, ... (AIMP SDK, C++ headers)
set(AIMP_SDK_DIR "" CACHE PATH "Path to the AIMP SDK C++ headers")
if(NOT AIMP_SDK_DIR OR NOT EXISTS "${AIMP_SDK_DIR}/apiPlugin.h")
    message(FATAL_ERROR "Set -DAIMP_SDK_DIR=<folder containing apiPlugin.h>")
endif()

add_library(aimp_discord_rpc SHARED
    src/plugin.cpp
    src/presence.cpp
    src/discord_ipc.cpp
    src/cover.cpp
    src/web.cpp
    src/settings_ui.cpp
    src/config.cpp
    src/util.cpp)

# some SDK versions ship helper code in apiTypes.cpp - compile it if present
if(EXISTS "${AIMP_SDK_DIR}/apiTypes.cpp")
    target_sources(aimp_discord_rpc PRIVATE "${AIMP_SDK_DIR}/apiTypes.cpp")
endif()

target_include_directories(aimp_discord_rpc PRIVATE "${AIMP_SDK_DIR}" "${AIMP_SDK_DIR}/Helpers" src)
target_compile_definitions(aimp_discord_rpc PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(aimp_discord_rpc PRIVATE
    winhttp gdiplus shlwapi shell32 comctl32 uxtheme ole32 user32 gdi32)
set_target_properties(aimp_discord_rpc PROPERTIES PREFIX "" OUTPUT_NAME "aimp_discord_rpc")
