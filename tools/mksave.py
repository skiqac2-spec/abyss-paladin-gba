#!/usr/bin/env python3
"""Create a test save: mksave.py out.sav diff act level wtier atier deepest"""
import sys
out, diff, act, level, wt, at, deep = sys.argv[1], *map(int, sys.argv[2:8])
b = bytearray(b"ABP1") + bytes([diff, act, level, wt, at, 3, 2, 2, 1, 5, 5, deep]) + (0).to_bytes(4, "little") + (500).to_bytes(3, "little")
c = 0x5A
for v in b[:23]:
    c = (c * 31 + v) & 255
b.append(c)
sav = bytearray(b"\xff" * 65536)
sav[0:24] = b
sav[0x100:0x118] = b
open(out, "wb").write(sav)
