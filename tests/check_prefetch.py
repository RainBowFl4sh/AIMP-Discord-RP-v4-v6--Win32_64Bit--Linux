#!/usr/bin/env python3
"""Checks an AIMP_TEST_PREFETCH=1 run (tests/run_*_test.sh), new in 1.6.5.

Usage: check_prefetch.py <frames of this run> <web log of this run>
- "Innuendo" was AIMP's next track while another one played: its folder image was uploaded in advance, so the very
  first presence of "Innuendo" already has the cover (before: the AIMP logo first, the cover seconds later)
- "Odd Song": Deezer only found an album of another artist - not used, the AIMP logo is shown
- "Exponat": the artist "ЛЕНИНГРАД" is "Ленинград" on Deezer - the same artist, its cover is used
- "Waiting": the cover source hangs for 25 s; the next track ("Bohemian Rhapsody") is shown within a few seconds
"""
import json
import sys

frames = []
for line in open(sys.argv[1], encoding="utf-8"):
    try:
        m = json.loads(line)
        a = m["data"]["args"].get("activity")
    except Exception:
        continue
    if a:
        frames.append((m.get("t", 0), a.get("details", ""), (a.get("assets") or {}).get("large_image", "")))
web = open(sys.argv[2], encoding="utf-8").read()

ok = True
def check(cond, good, bad):
    global ok
    print(("ok: " + good) if cond else ("FAILED: " + bad))
    ok = ok and cond

def of(title):
    return [f for f in frames if f[1] == title]

inn = of("Innuendo")
check(inn and inn[0][2].startswith("https://files.catbox.moe/"),
      "the next track's cover was ready in advance: first presence of Innuendo shows %s" % (inn[0][2] if inn else ""),
      "first presence of Innuendo without its cover: %s" % (inn[0][2] if inn else "no frame"))
check(web.count("/catbox.moe/user/api.php") == 1, "the cover of the next track was uploaded once",
      "uploads to catbox.moe: %d (expected 1)" % web.count("/catbox.moe/user/api.php"))
odd = of("Odd Song")
check(odd and all(f[2] == "aimp" for f in odd), "Deezer's hit by another artist was refused (AIMP logo)",
      "Odd Song showed %s" % sorted(set(f[2] for f in odd)))
exp = of("Exponat")
check(exp and any("dzcdn.net" in f[2] for f in exp), "Cyrillic artist in another case: Deezer's cover used",
      "Exponat showed %s" % sorted(set(f[2] for f in exp)))
wait = of("Waiting")
after = [f for f in of("Bohemian Rhapsody") if wait and f[0] > wait[0][0]]
gap = after[0][0] - wait[0][0] if wait and after else 999
check(gap < 8, "a hanging cover search did not hold up the next track (shown %.1f s after the slow one)" % gap,
      "the next track came %.1f s after the slow one (the source hangs 25 s)" % gap)
sys.exit(0 if ok else 1)
