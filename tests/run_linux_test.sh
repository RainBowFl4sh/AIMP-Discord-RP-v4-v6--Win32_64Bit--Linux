#!/usr/bin/env bash
# End-to-end test of the native Linux plugin without AIMP, Discord or internet:
#   mock AIMP host (tests/host_test.cpp) + fake Discord socket (tests/fake_discord.py) + fake web (tests/fake_web.py)
# Usage: tests/run_linux_test.sh <build dir> [output dir for the preview images]
set -euo pipefail

BUILD="$(cd "${1:-build}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
OUT="${2:-$TMP/out}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
PIDS=()
cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done   # never "kill 0": that hits the process group
    rm -rf "$TMP"
}
trap cleanup EXIT
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY all_proxy no_proxy NO_PROXY   # local servers only

export XDG_RUNTIME_DIR="$TMP/run" XDG_CONFIG_HOME="$TMP/config" AIMP_TEST_OUT="$OUT" AIMP_TEST_MARKER="$TMP/opened.txt"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/AIMP"
# settings of an older version (no ConfigVersion): covers only from (fake) Deezer, update check at every start
printf '[DiscordRPC]\nCoverEnabled=1\nCoverItunes=0\nCoverBandcamp=0\nCoverMusicBrainz=0\nUploadHost=0\nLargeText=%%album%% / %%playlist%%\nUpdateCheck=1\nUpdateFrequency=0\nUpdateAuto=1\nLastVersion=1.4.1\n' \
    > "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini"

python3 "$HERE/fake_discord.py" "$XDG_RUNTIME_DIR/discord-ipc-0" "$TMP/frames.log" &
PIDS+=($!)
python3 "$HERE/fake_web.py" "$TMP/port" "$TMP/web.log" &
PIDS+=($!)
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/discord-ipc-0" ] && [ -f "$TMP/port" ] && break; sleep 0.1; done
export AIMP_DISCORD_RPC_TEST_URL="http://127.0.0.1:$(cat "$TMP/port")"

# the update run "installs" the package over the plugin file: a copy, the build stays untouched
mkdir -p "$TMP/plugin"
cp "$BUILD/aimp_discord_rpc.so" "$TMP/plugin/"
AIMP_DISCORD_RPC_DEBUG=1 AIMP_TEST_INSTALL="$TMP/plugin/aimp_discord_rpc.so" "$BUILD/host_test" "$TMP/plugin/aimp_discord_rpc.so"
[ -f "$TMP/plugin/aimp_discord_rpc.so.old" ] || { echo "FAILED: the update package was not installed"; exit 1; }
python3 "$HERE/check_results.py" "$TMP/frames.log" "$TMP/web.log" "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini" \
    "$OUT/exported-settings.ini" "$TMP/opened.txt" "$XDG_CONFIG_HOME/AIMP/DiscordRPC"

# a local cover (folder image) is uploaded to (fake) catbox.moe - PHP parses the upload like the real service
mkdir -p "$TMP/music/Queen"
python3 -c "import sys; sys.path.insert(0, '$HERE'); import fake_web; open(sys.argv[1], 'wb').write(fake_web.png((10, 120, 200)))" "$TMP/music/Queen/cover.png"
: > "$TMP/music/Queen/01.flac"
printf '[DiscordRPC]\nConfigVersion=6\nCoverEnabled=1\nCoverPreferLocal=1\nCoverFolder=1\nUploadHost=1\nCoverDeezer=0\nCoverItunes=0\nCoverBandcamp=0\nCoverMusicBrainz=0\nUpdateCheck=0\n' \
    > "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini"
rm -rf "$XDG_CONFIG_HOME/AIMP/DiscordRPC" "$XDG_CONFIG_HOME/AIMP/DiscordRPC_covers.tsv"   # no cached cover
AIMP_TEST_UPLOAD=1 AIMP_TEST_TRACK="$TMP/music/Queen/01.flac" "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so"
grep -q "catbox.moe/user/api.php .* -> https://" "$TMP/web.log" || { echo "FAILED: catbox upload"; grep catbox "$TMP/web.log"; exit 1; }
# catbox.moe delivers empty files (as in October 2026): the upload is checked, x0.at stands in
: > "$TMP/web.log.catbox-broken"
: > "$TMP/web.log.proxy-broken"   # and Discord cannot load the first URL ("?" in Discord): "?r=1" repairs it
rm -rf "$XDG_CONFIG_HOME/AIMP/DiscordRPC" "$XDG_CONFIG_HOME/AIMP/DiscordRPC_covers.tsv"
AIMP_TEST_UPLOAD=1 AIMP_TEST_UPLOAD_HOST=x0.at AIMP_TEST_TRACK="$TMP/music/Queen/01.flac" "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so"
grep -q "^/x0.at/ .* -> https://x0.at/" "$TMP/web.log" || { echo "FAILED: x0.at fallback"; grep -e catbox -e x0.at "$TMP/web.log"; exit 1; }
grep -q 'T3st.png?r=1' "$TMP/frames.log" || { echo "FAILED: cover not repaired for Discord"; grep -o 'large_image[^,]*' "$TMP/frames.log" | tail -5; exit 1; }
rm -f "$TMP/web.log.catbox-broken" "$TMP/web.log.proxy-broken"

# texts with variants ("a || b") switch every RotateSeconds; own name instead of "AIMP"; title changes; tag reads
printf '[DiscordRPC]\nConfigVersion=7\nCoverEnabled=0\nDetails=%%artist%% || %%title%%\nState=%%title%%\nActivityName=%%album%%\nRotateSeconds=5\nUpdateCheck=0\n' > "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini"
before=$(wc -l < "$TMP/frames.log")
AIMP_TEST_ROTATE=1 "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so" > "$TMP/rotate.out"
tail -n +$((before + 1)) "$TMP/frames.log" > "$TMP/rotate.frames"
python3 "$HERE/check_rotation.py" "$TMP/rotate.frames" "$TMP/rotate.out" || exit 1

# AIMP reports a profile folder (portable AIMP): the settings move there once, the old file stays
printf '[DiscordRPC]\nConfigVersion=6\nDetails=profile test\nUpdateCheck=0\n' > "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini"
mkdir -p "$TMP/profile"
AIMP_TEST_PROFILE="$TMP/profile/" AIMP_TEST_UI_ONLY=1 AIMP_TEST_OUT="$TMP" "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so" > /dev/null
grep -q "^Details=profile test" "$TMP/profile/DiscordRPC.ini" || { echo "FAILED: settings not taken over into AIMP's profile folder"; exit 1; }
echo "profile folder: settings taken over"

# the settings page follows AIMP's language (here: Russian)
for lang in "ru:Русский" "de:Deutsch" "uk:Українська"; do
    rm -rf "$XDG_CONFIG_HOME/AIMP"
    mkdir -p "$XDG_CONFIG_HOME/AIMP" "$OUT/${lang%%:*}"
    AIMP_TEST_LANGUAGE="${lang#*:}" AIMP_TEST_UI_ONLY=1 AIMP_TEST_OUT="$OUT/${lang%%:*}" \
        "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so"
done
echo "OK: Linux end-to-end test passed (preview images in $OUT)"
