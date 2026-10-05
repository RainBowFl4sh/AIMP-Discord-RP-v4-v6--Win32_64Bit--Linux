#!/usr/bin/env python3
"""Checks what the plugin did in an end-to-end test run (tests/run_linux_test.sh, tests/run_wine_test.sh).

Usage: check_results.py <frames.log> <web.log> <DiscordRPC.ini> <exported.ini> <marker file> <cache dir>
"""
import json
import os
import sys

frames_log, web_log, ini_path, exported_path, marker, cache_dir = sys.argv[1:7]


def text(path):
    data = open(path, "rb").read()
    return data.decode("utf-16") if data[:2] == b"\xff\xfe" else data.decode("utf-8-sig")


frames = [json.loads(l) for l in open(frames_log, encoding="utf-8")]
for f in frames:
    print("frame:", json.dumps(f, ensure_ascii=False))
handshakes = [f for f in frames if f["op"] == 0]
assert handshakes and handshakes[0]["data"]["client_id"] == "1555109720807702559", "no handshake"
assert len(handshakes) >= 2, "Reconnect did not reconnect"
acts = [f["data"]["args"] for f in frames if f["op"] == 1 and f["data"].get("cmd") == "SET_ACTIVITY"]
shown = [a["activity"] for a in acts if "activity" in a]
assert shown, "presence was never set"

# the normal presence: texts from the settings page and the INI, playlist name, clickable title
a = shown[0]
assert a["details"] == "Bohemian Rhapsody – Remastered [test]", a
assert a["state"] == "by Queen", a
assert a["timestamps"]["end"] - a["timestamps"]["start"] == 354, a
assert a["assets"]["large_text"] == "A Night at the Opera / Test Mix", a
assert a["assets"]["small_image"] == "play", a
assert a["details_url"].startswith("https://www.youtube.com/results?search_query=Queen+Bohemian"), a
# the cover found on (fake) Deezer
assert any(s["assets"]["large_image"].startswith("https://e-cdns-images.dzcdn.net/") for s in shown), "no cover"

# playlist filter: cleared while playing, shown again; then the test presence
order = ["test" if "activity" in x and x["activity"].get("state") == "Test – the connection works!"
         else ("show" if "activity" in x else "clear") for x in acts]
print("sequence:", " ".join(order))
first_test = order.index("test")
assert "clear" in order[1:first_test] and order[first_test - 1] == "show", "playlist filter did not hide / show"
assert order[-1] == "clear", "presence not cleared on pause / exit"

# web requests: update check, package, pictures, cover lookup
web = open(web_log).read()
for part in ["/api.github.com/repos/RainBowFl4sh/AIMP-Discord-RP-v4-v6--Win32_64Bit--Linux/releases/latest",
             "/releases/download/v9.9.9/aimp_discord_rpc.aimppack", "/avatars.githubusercontent.com/u/66131975",
             "/discord.com/api/v9/oauth2/applications/1555109720807702559/assets", "/cdn.discordapp.com/app-assets/",
             "/cdn.discordapp.com/avatars/1/abc123.png", "/api.deezer.com/search/album", "/e-cdns-images.dzcdn.net/"]:
    assert part in web, "not requested: " + part

# the update: downloaded into the cache folder, checked and opened with "AIMP" (host_test)
opened = open(marker, encoding="utf-8", errors="replace").read().strip()
print("opened by AIMP:", opened)
assert opened.endswith("aimp_discord_rpc-9.9.9.aimppack"), opened
pkg = open(os.path.join(cache_dir, "aimp_discord_rpc-9.9.9.aimppack"), "rb").read()
assert pkg[:4] == b"PK\x03\x04" and b"Version: 9.9.9" in pkg, "package content"

ini = text(ini_path)
for part in ["ConfigVersion=6", "CoverEnabled=1", "Details=%title% [test]", "LargeText=%album% / %playlist%",
             "ExcludePlaylists=\n", "UpdateLatest=9.9.9", "UpdateOffered=9.9.9", "LastVersion=1.5"]:
    assert part in ini.replace("\r\n", "\n"), "INI: " + part
exported = text(exported_path)
assert "[DiscordRPC]" in exported and "Details=%title% [exported]" in exported and "UpdateLastCheck" not in exported, exported
print("OK: %d frames, %d web requests, update opened, settings file complete" % (len(frames), len(web.splitlines())))
