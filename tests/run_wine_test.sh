#!/usr/bin/env bash
# Runs the Windows DLL (x86 or x64) under Wine with the mock AIMP host (tests/host_test.cpp).
# There is no Discord named pipe in Wine, so this also tests the fallback to the Linux Discord socket.
# Usage: tests/run_wine_test.sh <build dir>   (contains aimp_discord_rpc.dll and host_test.exe, e.g. MinGW build)
set -euo pipefail

BUILD="$(cd "${1:-build}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
SRV=""
cleanup() {
    if [ -n "$SRV" ]; then kill "$SRV" 2>/dev/null || true; fi   # never "kill 0": that hits the whole process group
    wineserver -k 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT

export WINEPREFIX="$TMP/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
export XDG_RUNTIME_DIR="$TMP/run"
mkdir -p "$XDG_RUNTIME_DIR"
command -v wine >/dev/null || { echo "wine is not installed" >&2; exit 1; }
wineboot -i >/dev/null 2>&1 || true

APPDATA_DIR="$(ls -d "$WINEPREFIX"/drive_c/users/*/AppData/Roaming 2>/dev/null | head -1 || true)"
if [ -z "$APPDATA_DIR" ]; then echo "Wine prefix was not created (wineboot failed)" >&2; exit 1; fi
mkdir -p "$APPDATA_DIR/AIMP"
printf '[DiscordRPC]\r\nCoverEnabled=0\r\n' > "$APPDATA_DIR/AIMP/DiscordRPC.ini"   # no network in the test

python3 "$HERE/fake_discord.py" "$XDG_RUNTIME_DIR/discord-ipc-0" "$TMP/frames.log" &
SRV=$!
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/discord-ipc-0" ] && break; sleep 0.1; done

cp "$BUILD/aimp_discord_rpc.dll" "$BUILD/host_test.exe" "$TMP/"
(cd "$TMP" && wine host_test.exe aimp_discord_rpc.dll)

python3 - "$TMP/frames.log" <<'EOF'
import json, sys
frames = [json.loads(l) for l in open(sys.argv[1])]
for f in frames:
    print("frame:", json.dumps(f, ensure_ascii=False))
assert frames[0]["op"] == 0 and frames[0]["data"]["client_id"], "no handshake"
acts = [f["data"]["args"] for f in frames if f["op"] == 1 and f["data"].get("cmd") == "SET_ACTIVITY"]
shown = [a["activity"] for a in acts if "activity" in a]
assert shown, "presence was never set"
a = shown[0]
assert a["details"] == "Bohemian Rhapsody – Remastered", a
assert a["state"] == "by Queen", a
assert a["timestamps"]["end"] - a["timestamps"]["start"] == 354, a
assert "activity" not in acts[-1], "presence not cleared on pause / exit"
print("OK: %d frames via Wine -> Linux Discord socket" % len(frames))
EOF
