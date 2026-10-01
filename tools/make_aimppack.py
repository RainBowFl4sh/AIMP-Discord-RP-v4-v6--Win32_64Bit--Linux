#!/usr/bin/env python3
"""Builds aimp_discord_rpc.aimppack (a ZIP archive) with all platforms in one package:

    aimp_discord_rpc/
        aimp_discord_rpc.dll        Windows 32-bit
        aimp_discord_rpc.so         Linux x86_64
        aimp_discord_rpc.txt        package info (name, version, author, description)
        x64/
            aimp_discord_rpc.dll    Windows 64-bit
            aimp_discord_rpc.so     Linux x86_64 (copy)

AIMP for Windows picks the DLL matching its architecture (x86 at the root, x64 in the x64 folder) and ignores the
.so files. The Linux layout is not documented by AIMP, so the .so is placed in both folders.

Usage: tools/make_aimppack.py --x86 <dll> --x64 <dll> [--linux <so>] [--out aimp_discord_rpc.aimppack]
"""
import argparse
import os
import re
import sys
import zipfile

NAME = "aimp_discord_rpc"


def read_version():
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "..", "src", "version.h"), encoding="utf-8") as f:
        m = re.search(r'AIMP_DISCORD_RPC_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else "0.0.0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x86", required=True, help="32-bit Windows DLL")
    ap.add_argument("--x64", required=True, help="64-bit Windows DLL")
    ap.add_argument("--linux", help="Linux x86_64 .so (optional)")
    ap.add_argument("--out", default=NAME + ".aimppack")
    args = ap.parse_args()

    for path in filter(None, [args.x86, args.x64, args.linux]):
        if not os.path.isfile(path):
            sys.exit("missing file: " + path)

    info = (
        "Name: Discord Rich Presence\r\n"
        f"Version: {read_version()}\r\n"
        "Author: Fl4sh\r\n"
        "Topic: https://github.com/RainBowFl4sh/AIMP-Discord-RP-v5.x-v6.x\r\n"
        "Description: Discord Rich Presence with progress bar and cover art (Windows x86/x64, Linux x86_64)\r\n"
    )
    entries = [
        (args.x86, f"{NAME}/{NAME}.dll"),
        (args.x64, f"{NAME}/x64/{NAME}.dll"),
    ]
    if args.linux:
        entries += [(args.linux, f"{NAME}/{NAME}.so"), (args.linux, f"{NAME}/x64/{NAME}.so")]

    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{NAME}/", b"")
        z.writestr(f"{NAME}/x64/", b"")
        for src, dst in entries:
            z.write(src, dst)
        z.writestr(f"{NAME}/{NAME}.txt", info)
    print("written", args.out)
    with zipfile.ZipFile(args.out) as z:
        for i in z.infolist():
            print(f"  {i.file_size:>9}  {i.filename}")


if __name__ == "__main__":
    main()
