#!/usr/bin/env python3
"""Korean dialogue data for ABYSS PALADIN.

Reads tools/story_ko.txt, word-wraps every line for the dialogue box, collects ONLY the characters
the script actually uses and renders them with the Galmuri11 pixel font (SIL OFL, tools/fonts) as a
2bpp glyph set: 1 = letter, 2 = dark outline.  Output: build/story.c, build/story.h
usage: gen_story.py build_dir
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else "build"
os.makedirs(OUT, exist_ok=True)
FONT = ImageFont.truetype(os.path.join(HERE, "fonts", "Galmuri11-ko.ttf"), 12)
LINE_W = 204          # usable pixel width of one dialogue line (26 tiles = 208 px)
GH = 14               # glyph cell height incl. outline
SCENES = ["intro", "boss", "bossdown", "outro", "ending"]
HEAL_MAX = 3
NEV = len(SCENES) + HEAL_MAX
C_NL, C_PAGE, C_END, C_SPK = 0xFFFD, 0xFFFE, 0xFFFF, 0xF000

# ---------------------------------------------------------------- parse script
scenes = {}           # (act, name) -> list of (speaker, text)
cur = None
spk = ""
for raw in open(os.path.join(HERE, "story_ko.txt"), encoding="utf-8"):
    line = raw.rstrip("\n").strip()
    if not line or line.startswith("#"):
        continue
    if line.startswith("=="):
        p = line[2:].split()
        cur = (int(p[0]), p[1])
        scenes[cur] = []
        spk = ""
        continue
    if line.startswith("*"):
        spk = line[1:].strip()
        continue
    if cur is None:
        raise SystemExit("story: text before the first '== act scene' line: " + line)
    scenes[cur].append((spk, line))

# ---------------------------------------------------------------- glyphs
def adv(ch):
    return int(round(FONT.getlength(ch))) if ch != " " else 4


def wrap(text):
    lines, curl, w = [], "", 0
    for word in text.split(" "):
        ww = sum(adv(c) for c in word)
        sep = adv(" ") if curl else 0
        if curl and w + sep + ww > LINE_W:
            lines.append(curl); curl, w = "", 0; sep = 0
        if ww > LINE_W:                       # very long word: break by character
            for c in word:
                if w + adv(c) > LINE_W:
                    lines.append(curl); curl, w = "", 0
                curl += c; w += adv(c)
            continue
        curl += (" " if curl else "") + word
        w += sep + ww
    if curl:
        lines.append(curl)
    return lines


chars = {}
glyphs = []           # list of (rows[GH] u32, advance)


def glyph_index(ch):
    if ch in chars:
        return chars[ch]
    im = Image.new("L", (16, GH), 0)
    ImageDraw.Draw(im).text((1, 0), ch, font=FONT, fill=255)
    body = np.array(im) > 127
    outl = np.zeros_like(body)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            outl |= np.roll(np.roll(body, dy, 0), dx, 1)
    outl &= ~body
    g = body.astype(np.uint32) + outl.astype(np.uint32) * 2
    rows = [int(sum(int(g[y, x]) << (2 * x) for x in range(16))) for y in range(GH)]
    chars[ch] = len(glyphs)
    glyphs.append((rows, adv(ch)))
    return chars[ch]


glyph_index(" ")
# "next page" marker: a small down-pointing triangle (custom glyph)
tri = np.zeros((GH, 16), dtype=np.uint32)
for y, (a, b) in enumerate([(2, 8), (3, 7), (4, 6), (5, 5)]):
    tri[6 + y, a:b + 1] = 1
for y in range(GH):
    for x in range(16):
        if tri[y, x] == 0 and any(0 <= y + dy < GH and 0 <= x + dx < 16 and tri[y + dy, x + dx] == 1
                                  for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
            tri[y, x] = 2
NEXT_GLYPH = len(glyphs)
glyphs.append(([int(sum(int(tri[y, x]) << (2 * x) for x in range(16))) for y in range(GH)], 8))

speakers = [""]
stream = []
events = [[0xFFFF] * NEV for _ in range(5)]
heal_count = [0] * 5
pages_total = 0


def emit_text(spk_name, text):
    global pages_total
    if spk_name not in speakers:
        speakers.append(spk_name)
    lines = wrap(text)
    for i in range(0, len(lines), 2):
        stream.append(C_SPK | speakers.index(spk_name))
        for k, ln in enumerate(lines[i:i + 2]):
            if k:
                stream.append(C_NL)
            stream.extend(glyph_index(c) for c in ln)
        stream.append(C_PAGE)
        pages_total += 1


for (act, name), items in sorted(scenes.items()):
    if not 1 <= act <= 5:
        raise SystemExit("story: act must be 1..5 (%r)" % act)
    if name == "heal":
        for k, (s, t) in enumerate(items[:HEAL_MAX]):
            events[act - 1][len(SCENES) + k] = len(stream)
            emit_text(s, t)
            stream.append(C_END)
        heal_count[act - 1] = min(len(items), HEAL_MAX)
        continue
    if name not in SCENES:
        raise SystemExit("story: unknown scene '%s' (use %s, heal)" % (name, ", ".join(SCENES)))
    events[act - 1][SCENES.index(name)] = len(stream)
    for s, t in items:
        emit_text(s, t)
    stream.append(C_END)

spk_stream, spk_off = [], []
for s in speakers:
    spk_off.append(len(spk_stream))
    spk_stream.extend(glyph_index(c) for c in s)
    spk_stream.append(C_END)

# ---------------------------------------------------------------- emit C
with open(os.path.join(OUT, "story.c"), "w") as f:
    f.write("// Generated by tools/gen_story.py from tools/story_ko.txt (font: Galmuri11, SIL OFL 1.1)\n")
    f.write('#include "story.h"\n')
    f.write("const unsigned int glyph_rows[%d][%d] = {\n" % (len(glyphs), GH))
    for rows, a in glyphs:
        f.write("{%s},\n" % ",".join("0x%x" % r for r in rows))
    f.write("};\n")
    f.write("const unsigned char glyph_adv[%d] = {%s};\n" % (len(glyphs), ",".join(str(a) for _, a in glyphs)))
    f.write("const unsigned short story_stream[%d] = {%s};\n" % (len(stream), ",".join(str(v) for v in stream)))
    f.write("const unsigned short story_events[5][%d] = {%s};\n" %
            (NEV, ",".join("{%s}" % ",".join(str(v) for v in e) for e in events)))
    f.write("const unsigned char story_heal_count[5] = {%s};\n" % ",".join(str(v) for v in heal_count))
    f.write("const unsigned short speaker_stream[%d] = {%s};\n" % (len(spk_stream), ",".join(str(v) for v in spk_stream)))
    f.write("const unsigned short speaker_off[%d] = {%s};\n" % (len(spk_off), ",".join(str(v) for v in spk_off)))
with open(os.path.join(OUT, "story.h"), "w") as f:
    f.write("#ifndef STORY_H\n#define STORY_H\n")
    f.write("#define GLYPH_H %d\n#define GLYPH_NEXT %d\n#define GLYPH_SPACE 0\n" % (GH, NEXT_GLYPH))
    f.write("#define ST_NL 0x%X\n#define ST_PAGE 0x%X\n#define ST_END 0x%X\n#define ST_SPK 0x%X\n" % (C_NL, C_PAGE, C_END, C_SPK))
    for i, s in enumerate(SCENES):
        f.write("#define EV_%s %d\n" % (s.upper(), i))
    f.write("#define EV_HEAL0 %d\n#define EV_COUNT %d\n" % (len(SCENES), NEV))
    f.write("extern const unsigned int glyph_rows[%d][%d];\n" % (len(glyphs), GH))
    f.write("extern const unsigned char glyph_adv[%d];\n" % len(glyphs))
    f.write("extern const unsigned short story_stream[%d];\n" % len(stream))
    f.write("extern const unsigned short story_events[5][%d];\n" % NEV)
    f.write("extern const unsigned char story_heal_count[5];\n")
    f.write("extern const unsigned short speaker_stream[%d];\n" % len(spk_stream))
    f.write("extern const unsigned short speaker_off[%d];\n" % len(spk_off))
    f.write("#endif\n")

# preview sheet of all glyphs
sheet = np.zeros((((len(glyphs) + 31) // 32) * GH, 32 * 16), dtype=np.uint8)
for i, (rows, a) in enumerate(glyphs):
    for y, r in enumerate(rows):
        for x in range(16):
            sheet[(i // 32) * GH + y, (i % 32) * 16 + x] = (r >> (2 * x)) & 3
os.makedirs(os.path.join(OUT, "preview"), exist_ok=True)
pal = np.array([[20, 24, 50], [240, 236, 224], [0, 0, 0], [255, 0, 255]], dtype=np.uint8)
Image.fromarray(pal[sheet]).resize((sheet.shape[1] * 2, sheet.shape[0] * 2), Image.NEAREST).save(
    os.path.join(OUT, "preview", "glyphs.png"))
print("story: %d glyphs (only the characters used), %d pages, %d speakers, %d bytes of text"
      % (len(glyphs), pages_total, len(speakers), len(stream) * 2))
