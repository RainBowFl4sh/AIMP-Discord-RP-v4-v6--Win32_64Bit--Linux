#!/usr/bin/env python3
"""Converts the preview images the Windows test host writes (32-bit BMP) to PNG. Usage: bmp2png.py <folder>"""
import os
import struct
import sys
import zlib


def convert(path):
    d = open(path, "rb").read()
    off = struct.unpack("<I", d[10:14])[0]
    w, h = struct.unpack("<ii", d[18:26])
    bpp = struct.unpack("<H", d[28:30])[0] // 8
    rows = []
    for y in range(abs(h)):
        r = d[off + y * w * bpp: off + (y + 1) * w * bpp]
        rows.append(b"\x00" + b"".join(bytes((r[x * bpp + 2], r[x * bpp + 1], r[x * bpp])) for x in range(w)))
    if h > 0:   # bottom-up
        rows.reverse()

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, abs(h), 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
    open(path[:-4] + ".png", "wb").write(png)
    os.remove(path)


for root, _, files in os.walk(sys.argv[1]):
    for f in files:
        if f.endswith(".bmp"):
            convert(os.path.join(root, f))
