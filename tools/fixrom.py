#!/usr/bin/env python3
"""Fix a GBA ROM header: optional logo copy from a reference ROM, title, complement checksum, pad.
usage: fixrom.py rom.gba TITLE CODE [logo_reference.gba]"""
import sys
rom = bytearray(open(sys.argv[1], "rb").read())
title = sys.argv[2].encode()[:12].ljust(12, b"\0")
code = sys.argv[3].encode()[:4].ljust(4, b"\0")
rom[0xA0:0xAC] = title
rom[0xAC:0xB0] = code
rom[0xB2] = 0x96
if len(sys.argv) > 4:
    ref = open(sys.argv[4], "rb").read()
    rom[0x04:0xA0] = ref[0x04:0xA0]
chk = 0
for b in rom[0xA0:0xBD]:
    chk = (chk - b) & 0xFF
rom[0xBD] = (chk - 0x19) & 0xFF
while len(rom) % 256:
    rom.append(0xFF)
open(sys.argv[1], "wb").write(rom)
print("fixed", sys.argv[1], len(rom), "bytes")
