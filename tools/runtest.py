#!/usr/bin/env python3
"""Scripted gameplay tests: resolves symbol addresses from the ELF, runs the headless emulator,
converts screenshots to PNG.  usage: runtest.py act save script_template outdir"""
import subprocess, sys, os, re
from PIL import Image

act, save, tmpl, outdir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
os.makedirs(outdir, exist_ok=True)
elf = "out/act%s.elf" % act
syms = {}
for line in subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3:
        syms[p[2]] = int(p[0], 16)


def addr(m):
    name, off = m.group(1), int(m.group(2) or 0)
    return "%08x" % (syms[name] + off)


script = open(tmpl).read()
script = re.sub(r"@(\w+)(?:\+(\d+))?", addr, script)
script = script.replace("OUT/", outdir + "/")
sp = os.path.join(outdir, "script.txt")
open(sp, "w").write(script)
r = subprocess.run(["tools/gbatest", "out/act%s.gba" % act, save, sp], capture_output=True, text=True)
print(r.stdout.strip())
for f in sorted(os.listdir(outdir)):
    if f.endswith(".ppm"):
        Image.open(os.path.join(outdir, f)).resize((480, 320), Image.NEAREST).save(os.path.join(outdir, f[:-4] + ".png"))
        os.remove(os.path.join(outdir, f))
