#!/usr/bin/env python3
"""Checks the language files (langs/*.lng): every text the code uses exists in English, every language has the
same keys as English, and %1 / %2 placeholders match. Usage: tests/check_langs.py (from the repository root)"""
import glob
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else "."


def texts(path):
    out, section = {}, None
    for line in open(path, encoding="utf-8-sig"):
        line = line.strip()
        if not line or line[0] in ";#":
            continue
        if line.startswith("["):
            section = line.lower()
        elif section == "[discordrpc]" and "=" in line:
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


en = texts(f"{root}/langs/english.lng")
source = "".join(open(p, encoding="utf-8").read() for p in glob.glob(f"{root}/src/*.cpp"))
used = set(re.findall(r'"((?:Tab|Gen|Disp|Cov|Src|Adv|About|Upd|St|Prev|Status|Test)\.[A-Za-z0-9]+)"', source))
errors = [f"english.lng: missing {k}" for k in sorted(used - set(en))]
errors += [f"english.lng: not used {k}" for k in sorted(set(en) - used)]
for path in sorted(glob.glob(f"{root}/langs/*.lng")):
    lang = texts(path)
    name = path.split("/")[-1]
    errors += [f"{name}: missing {k}" for k in sorted(set(en) - set(lang))]
    errors += [f"{name}: unknown {k}" for k in sorted(set(lang) - set(en))]
    errors += [f"{name}: placeholders differ in {k}" for k in en if k in lang and
               sorted(re.findall(r"%\d", en[k])) != sorted(re.findall(r"%\d", lang[k]))]
    print(f"{name}: {len(lang)} texts")
for e in errors:
    print("ERROR:", e)
sys.exit(1 if errors else 0)
