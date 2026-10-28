#!/usr/bin/env python3
"""Checks the frames of an AIMP_TEST_ROTATE run (tests/run_*_test.sh).

Usage: check_rotation.py <frames of this run> <host_test output>
Settings of the run: Details=%artist% || %title%, State=%title%, ActivityName=%album%, RotateSeconds=5.
- the first line takes turns: artist, title, artist, ...
- every activity carries the own name (%album%) instead of the application name
- a stream title that changes without a track event is shown within a few seconds, the next track at once
- the tags are not read on every poll (host prints "tag reads: N in 13 s")
"""
import json
import re
import sys

seen, states, names = [], [], set()
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        a = json.loads(line)["data"]["args"]["activity"]
    except Exception:
        continue
    if not a:
        continue
    names.add(a.get("name"))
    if not states or states[-1] != a.get("state"):
        states.append(a.get("state"))
    if not seen or seen[-1] != a.get("details"):
        seen.append(a.get("details"))
print("rotation:", seen)
print("names:", sorted(n or "" for n in names))
print("second line:", states)
ok = len(seen) >= 3 and seen[0] == "Queen" and seen[1].startswith("Bohemian") and seen[2] == "Queen"
if not ok:
    print("FAILED: texts did not rotate")
if names != {"A Night at the Opera"}:
    print("FAILED: own activity name missing"); ok = False
for title in ("Radio Song", "Next Song"):
    if title not in states:
        print("FAILED: changed title not shown:", title); ok = False
m = re.search(r"tag reads: (\d+) in 13 s", open(sys.argv[2], encoding="utf-8", errors="replace").read())
if not m:
    print("FAILED: no tag read count"); ok = False
else:
    reads = int(m.group(1))
    print("tag reads in 13 s of playback:", reads)
    if reads > 10:   # every 3 s + the start; before 1.5.4 it was every poll (13 / 26)
        print("FAILED: tags read on every poll"); ok = False
sys.exit(0 if ok else 1)
