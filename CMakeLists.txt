cmake_minimum_required(VERSION 3.20)
project(aimp_discord_rpc LANGUAGES CXX)

# Targets:
#   Windows x64 / x86 (Win32)  -> aimp_discord_rpc.dll   (MSVC: cmake -A x64 | -A Win32, or MinGW, see cmake/*.cmake)
#   Linux x86_64               -> aimp_discord_rpc.so    (native AIMP for Linux; build needs libcurl + cairo headers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# static runtime -> no VC++ redistributable needed on the user's PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Folder that contains apiPlugin.h, apiCore.h, ... (AIMP SDK, C++ headers)
set(AIMP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk" CACHE PATH "Path to the AIMP SDK C++ headers")
if(NOT EXISTS "${AIMP_SDK_DIR}/apiPlugin.h")
    message(FATAL_ERROR "Set -DAIMP_SDK_DIR=<folder containing apiPlugin.h>")
endif()

add_library(aimp_discord_rpc SHARED
    src/plugin.cpp
    src/settings_ui.cpp
    src/presence.cpp
    src/discord_ipc.cpp
    src/cover.cpp
    src/web.cpp
    src/config.cpp
    src/util.cpp)

# some SDK versions ship helper code in apiTypes.cpp - compile it if present
if(EXISTS "${AIMP_SDK_DIR}/apiTypes.cpp")
    target_sources(aimp_discord_rpc PRIVATE "${AIMP_SDK_DIR}/apiTypes.cpp")
endif()

target_include_directories(aimp_discord_rpc PRIVATE "${AIMP_SDK_DIR}" "${AIMP_SDK_DIR}/Helpers" src)
set_target_properties(aimp_discord_rpc PROPERTIES PREFIX "" OUTPUT_NAME "aimp_discord_rpc")

if(WIN32)
    target_sources(aimp_discord_rpc PRIVATE
        src/aimp_discord_rpc.def)   # undecorated export name, also for 32-bit (stdcall)
    target_compile_definitions(aimp_discord_rpc PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(aimp_discord_rpc PRIVATE
        winhttp gdiplus shlwapi ole32 user32 gdi32)
    if(MINGW)
        # the SDK maps _ctoll to the non-existing wtoll for non-MSVC compilers
        target_compile_definitions(aimp_discord_rpc PRIVATE wtoll=_wtoll)
        target_link_options(aimp_discord_rpc PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--enable-stdcall-fixup)
    endif()
else()
    # headers only: libcurl is loaded at run time (dlopen), cairo is just included by the SDK's apiTypes.h
    find_path(CURL_HEADER_DIR curl/curl.h REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CAIRO REQUIRED cairo)
    target_include_directories(aimp_discord_rpc PRIVATE ${CAIRO_INCLUDE_DIRS} ${CURL_HEADER_DIR})
    target_link_libraries(aimp_discord_rpc PRIVATE dl pthread)
    target_compile_options(aimp_discord_rpc PRIVATE -fvisibility=hidden -Wno-attributes)
    # libstdc++ is linked statically so the plugin does not depend on the distribution's compiler version
    target_link_options(aimp_discord_rpc PRIVATE -static-libstdc++ -static-libgcc -Wl,--no-undefined)
endif()

# Mock AIMP host for tests/run_linux_test.sh and tests/run_wine_test.sh (not part of the plugin, not built by default)
add_executable(host_test EXCLUDE_FROM_ALL tests/host_test.cpp "${AIMP_SDK_DIR}/apiTypes.cpp")
target_include_directories(host_test PRIVATE "${AIMP_SDK_DIR}")
if(WIN32)
    target_compile_definitions(host_test PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MINGW)
        target_compile_definitions(host_test PRIVATE wtoll=_wtoll)
        target_link_options(host_test PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(host_test PRIVATE ole32 oleaut32 user32)
else()
    target_include_directories(host_test PRIVATE ${CAIRO_INCLUDE_DIRS})
    target_compile_options(host_test PRIVATE -Wno-attributes)
    target_link_libraries(host_test PRIVATE dl)
endif()
