#!/usr/bin/env python3
"""Checks the frames of an AIMP_TEST_ADVANCED run (tests/run_*_test.sh): advanced placeholders (GitHub issue #4).

Usage: check_advanced.py <frames of this run>
Settings of the run: AdvancedPlaceholders=1, Details = the example of the issue (AIMP syntax, nested %IF...),
State=%composer% - %bitrate% kbps, LargeText=%samplerate%Hz / %bitdepth% bit (ConfigVersion 9: the units written in
the text are removed, the values bring them - 44.1kHz / 16bit). The host plays a track with play count 7, then 8 (same track), then a comment,
then neither (file size 13.52 MB). Then (Templates tab) a text for pause "Paused: %composer%" and the buttons are
applied and the track is paused and resumed, then "Changed %bitrate%" is applied and preset 1 (the issue's
template) loaded by its hot key. Then "Base %bitrate%" is applied with rule 1 "title contains rhapsody -> preset 1":
the hot key wins for this track, the next track gets preset 1 by the rule. Last the display is switched off and on by
its hot key (the activity is cleared once and comes back).
"""
import json
import sys

details, states, buttons, large = [], [], [], set()
clears_after_rule = 0
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        args = json.loads(line)["data"]["args"]
    except Exception:
        continue
    a = args.get("activity") if isinstance(args, dict) else None
    if not a:
        if "Base 320" in details:
            clears_after_rule += 1
        continue
    if not details or details[-1] != a.get("details"):
        details.append(a.get("details"))
    large.add(a.get("assets", {}).get("large_text"))
    if a.get("buttons"):
        buttons.append(a["buttons"])
    if not states or states[-1] != a.get("state"):
        states.append(a.get("state"))
print("first line:", details)
print("second line:", states)
want = ["This song has been played 7 times", "This song has been played 8 times", "Live at Wembley", "13.52 MB",
        "Paused: Freddie Mercury", "13.52 MB", "Changed 320", "13.52 MB", "Base 320", "13.52 MB"]
ok = details == want
if not ok:
    print("FAILED: first line should be", want)
print("large text:", sorted(t for t in large if t))
if "44.1kHz / 16bit" not in large:
    ok = False
    print("FAILED: large text should be 44.1kHz / 16bit (old units removed)")
if states != ["Freddie Mercury - 320 kbps"]:
    print("FAILED: second line (new fields)"); ok = False
print("cleared after the rule:", clears_after_rule)
if clears_after_rule < 1:
    print("FAILED: on / off hot key did not clear the activity"); ok = False
print("buttons:", buttons[-1] if buttons else None)
want_buttons = [{"label": "Lyrics on Genius", "url": "https://genius.com/search?q="},
                {"label": "Find on Spotify", "url": "https://open.spotify.com/search/"}]
if not buttons or len(buttons[-1]) != 2 or any(b["label"] != w["label"] or not b["url"].startswith(w["url"]) or
                                               len(b["url"]) <= len(w["url"]) for b, w in zip(buttons[-1], want_buttons)):
    print("FAILED: buttons (labels / links with artist and title)"); ok = False
sys.exit(0 if ok else 1)
