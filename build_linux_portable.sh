#!/usr/bin/env bash
# Builds the Linux plugin so that it loads on (almost) every distribution: compiled with zig (pip install ziglang)
# against glibc 2.17, libc++ linked in, only AIMPPluginGetHeader exported. This is the .so that is released.
# (A normal "cmake --build" on Linux links against the glibc of the build machine, e.g. 2.39 on Ubuntu 24.04,
#  and then does not load on older systems.)
#
# Needs: python3 + "pip install ziglang", the cairo and libcurl headers (libcairo2-dev, libcurl4-openssl-dev).
# Usage: tools/build_linux_portable.sh <cmake build dir> [output file]
#        (the build dir only has to be configured: cmake -S . -B build - it holds generated/embedded.h)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cd "${1:?usage: $0 <cmake build dir> [output]}" && pwd)"
OUT="${2:-$BUILD/portable/aimp_discord_rpc.so}"
[ -f "$BUILD/generated/embedded.h" ] || { echo "run cmake -S . -B $BUILD first" >&2; exit 1; }
python3 -m ziglang version >/dev/null 2>&1 || { echo "zig is missing: pip install ziglang" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# only the curl / cairo headers - not the build machine's C library headers
mkdir -p "$TMP/inc" "$TMP/lib" "$(dirname "$OUT")"
CURL_DIR="$(dirname "$(find /usr/include -name curl.h -path '*curl/curl.h' | head -1)")"
cp -r "$CURL_DIR" "$TMP/inc/curl"
CAIRO_DIR="$(dirname "$(find /usr/include -name cairo.h | head -1)")"
for dir in /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib; do   # the 64-bit one (not i386 multiarch)
    if [ -e "$dir/libcairo.so.2" ]; then ln -s "$dir/libcairo.so.2" "$TMP/lib/libcairo.so"; break; fi
done

python3 -m ziglang c++ -target x86_64-linux-gnu.2.17 -std=c++17 -Os -fno-exceptions -fno-rtti -fPIC -shared -fvisibility=hidden \
    -Wno-attributes -Wno-nullability-completeness -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--version-script="$ROOT/src/exports.map" -s \
    -I"$ROOT/sdk" -I"$ROOT/sdk/Helpers" -I"$ROOT/src" -I"$BUILD/generated" -I"$CAIRO_DIR" -I"$TMP/inc" \
    "$ROOT"/src/*.cpp "$ROOT/sdk/apiTypes.cpp" -L"$TMP/lib" -lcairo -o "$OUT" > "$TMP/log" 2>&1 ||
    { grep -v "nullability\|_Nonnull\|_Nullable\|^ *|\|^ *^\|In file included" "$TMP/log" | head -40; exit 1; }

echo "written $OUT ($(stat -c %s "$OUT") bytes), needs glibc $(objdump -T "$OUT" | grep -o 'GLIBC_[0-9.]*' |
    sort -t. -k2,2n -k3,3n -u | tail -1)"
