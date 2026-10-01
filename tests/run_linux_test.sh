#!/usr/bin/env bash
# End-to-end test of the native Linux plugin without AIMP / Discord:
#   mock AIMP host (tests/host_test.cpp) + fake Discord socket (tests/fake_discord.py)
# Usage: tests/run_linux_test.sh <build dir>   (the build dir must contain aimp_discord_rpc.so and host_test)
set -euo pipefail

BUILD="$(cd "${1:-build}" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
SRV=""
cleanup() {
    if [ -n "$SRV" ]; then kill "$SRV" 2>/dev/null || true; fi   # never "kill 0": that hits the whole process group
    rm -rf "$TMP"
}
trap cleanup EXIT

export XDG_RUNTIME_DIR="$TMP/run" XDG_CONFIG_HOME="$TMP/config"
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/AIMP"
# no network in the test: switch cover lookups off
printf '[DiscordRPC]\nCoverEnabled=0\n' > "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini"

python3 "$HERE/fake_discord.py" "$XDG_RUNTIME_DIR/discord-ipc-0" "$TMP/frames.log" &
SRV=$!
for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/discord-ipc-0" ] && break; sleep 0.1; done

AIMP_DISCORD_RPC_DEBUG=1 "$BUILD/host_test" "$BUILD/aimp_discord_rpc.so"

python3 - "$TMP/frames.log" "$XDG_CONFIG_HOME/AIMP/DiscordRPC.ini" <<'EOF'
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
assert a["assets"]["large_image"] == "aimp" and a["assets"]["small_image"] == "play", a
assert "activity" not in acts[-1], "presence not cleared on pause / exit"
ini = open(sys.argv[2]).read()
assert "coverenabled=0" in ini and "configversion=4" in ini, ini
print("OK: %d frames, presence set and cleared, settings file complete" % len(frames))
EOF
