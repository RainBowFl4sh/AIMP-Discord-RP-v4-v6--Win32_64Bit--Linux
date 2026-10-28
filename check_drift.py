#!/usr/bin/env python3
"""Checks the frames of an AIMP_TEST_DRIFT run (tests/run_*_test.sh).

Usage: check_drift.py <frames of this run>
- "Speed": AIMP plays at speed 1.25 and says so - Discord's start stays put (at most one correction on a busy
  machine), the bar lasts 300 / 1.25 = 240 s
- "Drift": AIMP plays faster without a reason the plugin knows - it corrects Discord's start, the first times at
  once, then at most every 15 seconds (1.6.2 sent every 2 seconds)
"""
import json
import sys

runs = {"Speed": [], "Drift": []}
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        m = json.loads(line)
        a = m["data"]["args"]["activity"]
    except Exception:
        continue
    if a and a.get("timestamps") and a.get("details") in runs:
        runs[a["details"]].append((m.get("t", 0), a["timestamps"]["start"], a["timestamps"].get("end", 0)))

ok = True
speed = runs["Speed"]
if not speed:
    print("FAILED: no frames while playing at speed 1.25")
    sys.exit(1)
moves = sum(1 for a, b in zip(speed, speed[1:]) if abs(b[1] - a[1]) >= 300)
lengths = sorted(set(e - s for _, s, e in speed))
print("speed: %d frames, %d correction(s), Discord's bar %s ms" % (len(speed), moves, lengths))
if moves > 1:   # one may happen on a busy machine (the mock's clock lags); 1.6.2 corrected every 2 seconds
    print("FAILED: the start keeps moving although AIMP reported its speed")
    ok = False
if any(abs(n - 240000) > 5 for n in lengths):
    print("FAILED: the bar does not last 300 s / 1.25")
    ok = False

drift = runs["Drift"]
fixes = [t for (t, s, _), (_, p, _) in zip(drift[1:], drift) if abs(s - p) >= 300]
gaps = [round(b - a, 1) for a, b in zip(fixes, fixes[1:])]
print("drift: %d frames, %d corrections, gaps %s s" % (len(drift), len(fixes), gaps))
if len(fixes) < 3:
    print("FAILED: the drift is not corrected")
    ok = False
if len(fixes) > 6 or sum(1 for g in gaps if g < 14) > 2:
    print("FAILED: corrected too often (at most 3 at once, then every 15 s)")
    ok = False
print("OK: drift" if ok else "FAILED: drift")
sys.exit(0 if ok else 1)
