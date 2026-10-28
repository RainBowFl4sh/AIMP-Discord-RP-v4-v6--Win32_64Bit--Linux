#!/usr/bin/env bash
# Runs the Windows DLL (x86 or x64) under Wine with the mock AIMP host (tests/host_test.cpp), the fake Discord
# socket and the fake web server - the same checks as tests/run_linux_test.sh.
# There is no Discord named pipe in Wine, so this also tests the fallback to the Linux Discord socket.
# Usage: tests/run_wine_test.sh <build dir> [output dir for the preview images]
#        (the build dir contains aimp_discord_rpc.dll and host_test.exe, e.g. a MinGW build)
set -euo pipefail
# the Windows file dialog (export / import) needs a screen: use a virtual one when there is none
if [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null; then exec xvfb-run -a bash "$0" "$@"; fi

BUILD="$(cd "${1:-build}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
OUT="${2:-$TMP/out}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
rm -f "$OUT/exported-settings.ini"   # left from an earlier run: the save dialog would ask before replacing it
PIDS=()
cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done   # never "kill 0": that hits the process group
    wineserver -k 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy no_proxy NO_PROXY   # local servers only

export WINEPREFIX="$TMP/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" LC_ALL=C.UTF-8   # UTF-8: Cyrillic environment values reach Windows programs intact
export XDG_RUNTIME_DIR="$TMP/run" AIMP_TEST_OUT="$OUT" AIMP_TEST_MARKER="$TMP/opened.txt"
mkdir -p "$XDG_RUNTIME_DIR"
command -v wine >/dev/null || { echo "wine is not installed" >&2; exit 1; }
wineboot -i >/dev/null 2>&1 || true

APPDATA_DIR="$(ls -d "$WINEPREFIX"/drive_c/users/*/AppData/Roaming 2>/dev/null | head -1 || true)"
if [ -z "$APPDATA_DIR" ]; then echo "Wine prefix was not created (wineboot failed)" >&2; exit 1; fi
mkdir -p "$APPDATA_DIR/AIMP"
# settings of an older version (ANSI file, no ConfigVersion): covers only from (fake) Deezer, update check at start
printf '[DiscordRPC]\r\nCoverEnabled=1\r\nCoverItunes=0\r\nCoverBandcamp=0\r\nCoverMusicBrainz=0\r\nUploadHost=0\r\nLargeText=%%album%% / %%playlist%%\r\nUpdateCheck=1\r\nUpdateFrequency=0\r\nUpdateAuto=1\r\nLastVersion=1.4.1\r\n' \
    > "$APPDATA_DIR/AIMP/DiscordRPC.ini"

python3 "$HERE/fake_discord.py" "$XDG_RUNTIME_DIR/discord-ipc-0" "$TMP/frames.log" &
PIDS+=($!)
python3 "$HERE/fake_web.py" "$TMP/port" "$TMP/web.log" &
PIDS+=($!)
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/discord-ipc-0" ] && [ -f "$TMP/port" ] && break; sleep 0.1; done
export AIMP_DISCORD_RPC_TEST_URL="http://127.0.0.1:$(cat "$TMP/port")"

cp "$BUILD/aimp_discord_rpc.dll" "$BUILD/host_test.exe" "$TMP/"
# the update run "installs" the package over the plugin file (the copy in $TMP)
(cd "$TMP" && AIMP_TEST_INSTALL="Z:$(echo "$TMP/aimp_discord_rpc.dll" | tr / '\\')" wine host_test.exe aimp_discord_rpc.dll)
[ -f "$TMP/aimp_discord_rpc.dll.old" ] || { echo "FAILED: the update package was not installed"; exit 1; }
python3 "$HERE/check_results.py" "$TMP/frames.log" "$TMP/web.log" "$APPDATA_DIR/AIMP/DiscordRPC.ini" \
    "$OUT/exported-settings.ini" "$TMP/opened.txt" "$APPDATA_DIR/AIMP/DiscordRPC"

# GitHub's API limit is used up (shared address): the release feed names the new version, the package is opened
: > "$TMP/web.log.api-limit"
rm -f "$TMP/opened.txt"
printf '[DiscordRPC]\r\nConfigVersion=10\r\nCoverEnabled=0\r\nUpdateCheck=1\r\nUpdateFrequency=0\r\nUpdateAuto=1\r\n' > "$APPDATA_DIR/AIMP/DiscordRPC.ini"
(cd "$TMP" && AIMP_TEST_UPDATE=1 wine host_test.exe aimp_discord_rpc.dll)
grep -q "releases.atom" "$TMP/web.log" && grep -q "v9.9.9/aimp_discord_rpc-9.9.9.aimppack" "$TMP/web.log" && grep -q "9.9.9.aimppack" "$TMP/opened.txt" \
    || { echo "FAILED: update through the release feed"; tail -5 "$TMP/web.log"; exit 1; }
rm -f "$TMP/web.log.api-limit"

# a local cover (folder image) is uploaded to (fake) catbox.moe through WinHTTP - PHP parses it like the real service
mkdir -p "$TMP/music/Queen"
python3 -c "import sys; sys.path.insert(0, '$HERE'); import fake_web; open(sys.argv[1], 'wb').write(fake_web.png((10, 120, 200)))" "$TMP/music/Queen/cover.png"
: > "$TMP/music/Queen/01.flac"
printf '[DiscordRPC]\r\nConfigVersion=6\r\nCoverEnabled=1\r\nCoverPreferLocal=1\r\nCoverFolder=1\r\nUploadHost=1\r\nCoverDeezer=0\r\nCoverItunes=0\r\nCoverBandcamp=0\r\nCoverMusicBrainz=0\r\nUpdateCheck=0\r\n' \
    > "$APPDATA_DIR/AIMP/DiscordRPC.ini"
rm -rf "$APPDATA_DIR/AIMP/DiscordRPC" "$APPDATA_DIR/AIMP/DiscordRPC_covers.tsv"   # no cached cover
(cd "$TMP" && AIMP_TEST_UPLOAD=1 AIMP_TEST_TRACK="Z:$(echo "$TMP/music/Queen/01.flac" | tr / '\\')" wine host_test.exe aimp_discord_rpc.dll)
grep -q "catbox.moe/user/api.php .* -> https://" "$TMP/web.log" || { echo "FAILED: catbox upload"; grep catbox "$TMP/web.log"; exit 1; }
# catbox.moe delivers empty files (as in October 2026): the upload is checked, x0.at stands in
: > "$TMP/web.log.catbox-broken"
: > "$TMP/web.log.proxy-broken"   # and Discord cannot load the first URL ("?" in Discord): "?r=1" repairs it
rm -rf "$APPDATA_DIR/AIMP/DiscordRPC" "$APPDATA_DIR/AIMP/DiscordRPC_covers.tsv"
(cd "$TMP" && AIMP_TEST_UPLOAD=1 AIMP_TEST_UPLOAD_HOST=x0.at AIMP_TEST_TRACK="Z:$(echo "$TMP/music/Queen/01.flac" | tr / '\\')" wine host_test.exe aimp_discord_rpc.dll)
grep -q "^/x0.at/ .* -> https://x0.at/" "$TMP/web.log" || { echo "FAILED: x0.at fallback"; grep -e catbox -e x0.at "$TMP/web.log"; exit 1; }
grep -q 'T3st.png?r=1' "$TMP/frames.log" || { echo "FAILED: cover not repaired for Discord"; grep -o 'large_image[^,]*' "$TMP/frames.log" | tail -5; exit 1; }
rm -f "$TMP/web.log.catbox-broken" "$TMP/web.log.proxy-broken"
# an Opus file (no folder image): the plugin cannot read its tags itself - AIMP's tag readers deliver the cover
mkdir -p "$TMP/music/Opus"
: > "$TMP/music/Opus/01.opus"
python3 -c "import sys; sys.path.insert(0, '$HERE'); import fake_web; open(sys.argv[1], 'wb').write(fake_web.png((200, 60, 140)))" "$TMP/tag-art.png"
rm -rf "$APPDATA_DIR/AIMP/DiscordRPC" "$APPDATA_DIR/AIMP/DiscordRPC_covers.tsv"
(cd "$TMP" && AIMP_TEST_UPLOAD=1 AIMP_TEST_TAG_ART="tag-art.png" AIMP_TEST_TRACK="Z:$(echo "$TMP/music/Opus/01.opus" | tr / '\\')" wine host_test.exe aimp_discord_rpc.dll > "$TMP/opus.out") \
    || { cat "$TMP/opus.out"; exit 1; }
grep "^upload:" "$TMP/opus.out"
grep -q "^upload: Source: from the file's tags, uploaded to catbox.moe (AIMP's tag readers asked 1 time" "$TMP/opus.out" \
    || { echo "FAILED: the cover AIMP read from the Opus file was not uploaded"; exit 1; }

# texts with variants ("a || b") switch every RotateSeconds; own name instead of "AIMP"; title changes; tag reads
printf '[DiscordRPC]\r\nConfigVersion=7\r\nCoverEnabled=0\r\nDetails=%%artist%% || %%title%%\r\nState=%%title%%\r\nActivityName=%%album%%\r\nRotateSeconds=5\r\nUpdateCheck=0\r\n' > "$APPDATA_DIR/AIMP/DiscordRPC.ini"
before=$(wc -l < "$TMP/frames.log")
(cd "$TMP" && AIMP_TEST_ROTATE=1 wine host_test.exe aimp_discord_rpc.dll > "$TMP/rotate.out")
tail -n +$((before + 1)) "$TMP/frames.log" > "$TMP/rotate.frames"
python3 "$HERE/check_rotation.py" "$TMP/rotate.frames" "$TMP/rotate.out" || exit 1

# advanced placeholders (GitHub issue #4): templates unit test, then more fields and nested %IF... end to end
cp "$BUILD/template_test.exe" "$TMP/"
(cd "$TMP" && wine template_test.exe) || exit 1
# AIMP's speed / tempo: Discord's bar follows it; a drift of unknown cause is corrected, but not all the time
printf '[DiscordRPC]\r\nConfigVersion=10\r\nCoverEnabled=0\r\nDetails=%%title%%\r\nUpdateCheck=0\r\n' > "$APPDATA_DIR/AIMP/DiscordRPC.ini"
before=$(wc -l < "$TMP/frames.log")
(cd "$TMP" && AIMP_TEST_DRIFT=1 wine host_test.exe aimp_discord_rpc.dll > /dev/null)
tail -n +$((before + 1)) "$TMP/frames.log" > "$TMP/drift.frames"
python3 "$HERE/check_drift.py" "$TMP/drift.frames" || exit 1

for formatter in 1 0; do
    printf '[DiscordRPC]\r\nConfigVersion=9\r\nCoverEnabled=0\r\nAdvancedPlaceholders=1\r\nDetails=%s\r\nState=%%composer%% - %%bitrate%% kbps\r\nLargeText=%%samplerate%%Hz / %%bitdepth%% bit\r\nUpdateCheck=0\r\n' \
        '%If(%Comment,%Comment,%IFEqual(%PlayCount,1,This song has been played once before,%IFGreater(%PlayCount,1,This song has been played %PlayCount times,%FileSize)))' > "$APPDATA_DIR/AIMP/DiscordRPC.ini"
    before=$(wc -l < "$TMP/frames.log")
    (cd "$TMP" && AIMP_TEST_ADVANCED=1 AIMP_TEST_FORMATTER=$formatter wine host_test.exe aimp_discord_rpc.dll > "$TMP/advanced.out") \
        || { cat "$TMP/advanced.out"; exit 1; }
    grep "^advanced:" "$TMP/advanced.out"
    tail -n +$((before + 1)) "$TMP/frames.log" > "$TMP/advanced.frames"
    python3 "$HERE/check_advanced.py" "$TMP/advanced.frames" || exit 1
done

for lang in "ru:Русский" "de:Deutsch" "uk:Українська"; do
    rm -rf "$APPDATA_DIR/AIMP"
    mkdir -p "$APPDATA_DIR/AIMP" "$OUT/${lang%%:*}"
    (cd "$TMP" && AIMP_TEST_LANGUAGE="${lang#*:}" AIMP_TEST_UI_ONLY=1 AIMP_TEST_OUT="$OUT/${lang%%:*}" \
        wine host_test.exe aimp_discord_rpc.dll)
done
python3 "$HERE/bmp2png.py" "$OUT"   # preview images: BMP from GDI -> PNG
echo "OK: Wine end-to-end test passed (preview images in $OUT)"
