#!/usr/bin/env python3
"""Generates all original pixel-art assets for ABYSS PALADIN as C source.

Everything here is drawn procedurally (no external/ripped graphics):
isometric dungeon tiles, the paladin, monsters, bosses, items, effects,
font, HUD and the title screen.  Output: build/assets.c and build/assets.h
"""
import os, sys, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1] if len(sys.argv) > 1 else "build"
PREVIEW = os.path.join(OUT, "preview")
os.makedirs(PREVIEW, exist_ok=True)

C = []   # C source chunks
H = []   # header chunks


def rgb15(c):
    r, g, b = c
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def emit_pal(name, cols):
    cols = list(cols) + [(0, 0, 0)] * (16 - len(cols))
    vals = ", ".join("0x%04X" % rgb15(c) for c in cols[:16])
    C.append("const unsigned short %s[16] = { %s };\n" % (name, vals))
    H.append("extern const unsigned short %s[16];\n" % name)


def emit_pal_array(name, pals):
    rows = []
    for cols in pals:
        cols = list(cols) + [(0, 0, 0)] * (16 - len(cols))
        rows.append("{ " + ", ".join("0x%04X" % rgb15(c) for c in cols[:16]) + " }")
    C.append("const unsigned short %s[%d][16] = {\n  %s\n};\n" % (name, len(pals), ",\n  ".join(rows)))
    H.append("extern const unsigned short %s[%d][16];\n" % (name, len(pals)))


def tiles_from_img(a):
    """a: 2D array of palette indices, size multiple of 8 -> list of u32 words (4bpp, row-major tiles)."""
    h, w = a.shape
    words = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            for y in range(8):
                v = 0
                for x in range(8):
                    v |= (int(a[ty * 8 + y, tx * 8 + x]) & 15) << (4 * x)
                words.append(v)
    return words


def emit_words(name, words):
    lines = []
    for i in range(0, len(words), 8):
        lines.append(", ".join("0x%08X" % w for w in words[i:i + 8]))
    C.append("const unsigned int %s[%d] = {\n  %s\n};\n" % (name, len(words), ",\n  ".join(lines)))
    H.append("extern const unsigned int %s[%d];\n" % (name, len(words)))


def save_preview(name, a, pal, scale=4):
    pal = list(pal) + [(0, 0, 0)] * (16 - len(pal))
    img = Image.new("RGB", (a.shape[1], a.shape[0]))
    px = img.load()
    for y in range(a.shape[0]):
        for x in range(a.shape[1]):
            v = int(a[y, x])
            px[x, y] = (255, 0, 255) if v == 0 and name != "nobg" else pal[v]
    img = img.resize((a.shape[1] * scale, a.shape[0] * scale), Image.NEAREST)
    img.save(os.path.join(PREVIEW, name + ".png"))


def h32(*v):
    x = 2166136261
    for n in v:
        x ^= (n & 0xFFFFFFFF)
        x = (x * 16777619) & 0xFFFFFFFF
        x ^= x >> 13
    return x

# ---------------------------------------------------------------------------
# Act palettes for the isometric map (BG palette bank 0)
#  0 void, 1-3 floor A dark/mid/light, 4 mortar, 5-7 floor B dark/mid/light,
#  8-9 wall top dark/mid, 10-11 face L dark/light, 12-13 face R dark/light,
#  14 brick mortar, 15 highlight
ACT_BG = [
    # Act 1 - Ashen Moor (grey stone, mossy)
    [(0, 0, 0), (54, 50, 46), (78, 72, 64), (104, 96, 84), (30, 27, 25), (44, 56, 34), (62, 78, 44), (86, 104, 58),
     (40, 36, 34), (62, 56, 52), (70, 60, 52), (104, 90, 76), (50, 42, 36), (78, 66, 56), (28, 22, 20), (150, 140, 120)],
    # Act 2 - Sunken Dunes (sandstone)
    [(0, 0, 0), (110, 88, 56), (146, 118, 76), (180, 150, 100), (66, 50, 30), (150, 120, 70), (186, 152, 92), (214, 186, 124),
     (84, 64, 40), (116, 90, 58), (130, 100, 62), (176, 140, 90), (98, 74, 46), (140, 108, 68), (60, 44, 26), (230, 210, 160)],
    # Act 3 - Mire Jungle (wet green stone)
    [(0, 0, 0), (40, 52, 42), (56, 72, 56), (76, 96, 72), (20, 28, 22), (34, 64, 38), (48, 90, 48), (70, 120, 60),
     (30, 40, 32), (46, 60, 46), (52, 70, 50), (78, 100, 70), (38, 52, 38), (60, 80, 56), (16, 24, 18), (130, 160, 110)],
    # Act 4 - Burning Steps (hell rock)
    [(0, 0, 0), (66, 30, 26), (92, 42, 34), (122, 58, 42), (36, 14, 12), (90, 40, 20), (140, 62, 24), (200, 100, 30),
     (48, 20, 18), (74, 32, 26), (90, 38, 30), (130, 58, 40), (66, 26, 22), (100, 44, 32), (30, 10, 8), (240, 160, 60)],
    # Act 5 - Frozen Crown (ice and snow)
    [(0, 0, 0), (110, 120, 136), (146, 158, 176), (186, 198, 214), (66, 74, 90), (170, 190, 210), (204, 220, 236), (236, 244, 252),
     (80, 90, 108), (110, 122, 142), (120, 134, 156), (168, 184, 206), (92, 104, 124), (136, 150, 174), (56, 62, 78), (250, 252, 255)],
]
# Town variants: lighter, outdoor ground in floor B (grass / sand / mud / ash / snow)
emit_pal_array("pal_act_bg", ACT_BG)

VOID, FLOOR, FLOOR2, WALL = 0, 1, 2, 3


def floor_tex(t, u, v):
    """Diamond-local texture. u in [-16,16], v in [-8,8]."""
    # world coords inside the tile 0..1
    wx = (u / 16.0 + v / 8.0) / 2 + 0.5
    wy = (v / 8.0 - u / 16.0) / 2 + 0.5
    if t == VOID:
        return 0
    n = h32(int(u * 2 + 40), int(v * 2 + 40), t) % 100
    if t == WALL:
        # rough dark stone blocks on the wall top
        sx, sy = int(wx * 3), int(wy * 3)
        e = min(abs(wx * 3 - round(wx * 3)), abs(wy * 3 - round(wy * 3)))
        if e < 0.09:
            return 14
        return 9 if (h32(sx, sy, 7) % 3 and n > 20) else 8
    # flagstones: 2x2 stones per diamond
    ex = min(abs(wx * 2 - round(wx * 2)), abs(wy * 2 - round(wy * 2)))
    if ex < 0.07:
        return 4
    sx, sy = int(wx * 2), int(wy * 2)
    stone = h32(sx, sy, t) % 3
    base = 1 if t == FLOOR else 5
    if t == FLOOR2 and n < 18:
        return 7
    if n < 6:
        return 4 if t == FLOOR else 5          # cracks / speckles
    lvl = stone + (1 if n > 85 else 0) - (1 if n < 15 else 0)
    lvl = max(0, min(2, lvl))
    # light from top-left: brighter near the upper-left edges of each stone
    fx, fy = wx * 2 - sx, wy * 2 - sy
    if fx < 0.18 or fy < 0.18:
        lvl = min(2, lvl + 1)
    return base + lvl


def face_tex(side, x, y, d):
    """Wall face. side 'L' (south-west facing) or 'R' (south-east facing).
    x,y rect-local pixel; d = depth below wall edge (0..8)."""
    if side == 'L':
        course = int(math.floor(d / 4.0))
        dark, light = 10, 11
    else:
        course = int(math.floor(d / 4.0))
        dark, light = 12, 13
    # mortar lines
    if (int(d) % 4) == 0 and d > 0.5:
        return 14
    if ((x + 4 * course) % 8) == 0:
        return 14
    if d > 6.5:
        return dark
    return light if (h32(x, int(d), course) % 7) else dark


def iso_tile(orient, side, tT, tB, xwall):
    """orient 0 = '\\' (top diamond at rect TL), 1 = '/' (top diamond at rect TR)."""
    a = np.zeros((8, 8), dtype=np.uint8)
    for ly in range(8):
        for lx in range(8):
            x = side * 8 + lx + 0.5
            y = ly + 0.5
            if orient == 0:
                inT = (x + 2 * y) <= 16
                if inT:
                    owner, u, v = tT, x, y
                else:
                    owner, u, v = tB, x - 16, y - 8
                faceX = (y <= x / 2) and xwall                 # X's SW face (L)
                dX = y - (x / 2 - 8)
                faceT = (not inT) and tT == WALL               # T's SE face (R)
                dT = y - 8 + x / 2
                fT_side, fX_side = 'R', 'L'
            else:
                inT = (2 * y) <= x
                if inT:
                    owner, u, v = tT, x - 16, y
                else:
                    owner, u, v = tB, x, y - 8
                faceX = (y <= 8 - x / 2) and xwall             # X's SE face (R)
                dX = y + x / 2
                faceT = (not inT) and tT == WALL               # T's SW face (L)
                dT = y - x / 2
                fT_side, fX_side = 'L', 'R'
            if owner == WALL:
                col = floor_tex(WALL, u, v)
            elif faceT:
                col = face_tex(fT_side, int(x), int(y), dT)
            elif faceX and not (inT and tT == WALL):
                col = face_tex(fX_side, int(x), int(y), dX)
            else:
                col = floor_tex(owner, u, v)
            a[ly, lx] = col
    return a


iso_words = []
iso_sheet = np.zeros((8 * 8, 16 * 8), dtype=np.uint8)
idx = 0
for orient in range(2):
    for side in range(2):
        for tT in range(4):
            for tB in range(4):
                for xw in range(2):
                    t = iso_tile(orient, side, tT, tB, xw)
                    iso_words += tiles_from_img(t)
                    iso_sheet[(idx // 16) * 8:(idx // 16) * 8 + 8, (idx % 16) * 8:(idx % 16) * 8 + 8] = t
                    idx += 1
emit_words("iso_tiles", iso_words)
save_preview("iso_tiles", iso_sheet, ACT_BG[0])


# Preview: render a small iso room using the same rule the C engine uses
def preview_room(act):
    W = 10
    m = [[VOID] * 14 for _ in range(14)]
    for i in range(2, 12):
        for j in range(2, 12):
            m[i][j] = FLOOR if (i * 7 + j * 3) % 5 else FLOOR2
    for i in range(1, 13):
        for j in range(1, 13):
            if m[i][j] == VOID:
                near = any(0 <= i + di < 14 and 0 <= j + dj < 14 and m[i + di][j + dj] in (FLOOR, FLOOR2)
                           for di in (-1, 0, 1) for dj in (-1, 0, 1))
                if near:
                    m[i][j] = WALL
    m[6][6] = WALL; m[6][7] = WALL; m[7][6] = WALL

    def T(i, j):
        if 0 <= i < 14 and 0 <= j < 14:
            return m[i][j]
        return VOID
    tiles = {}
    k = 0
    for orient in range(2):
        for side in range(2):
            for tT in range(4):
                for tB in range(4):
                    for xw in range(2):
                        tiles[(orient, side, tT, tB, xw)] = iso_tile(orient, side, tT, tB, xw)
    img = np.zeros((160, 240), dtype=np.uint8)
    camx, camy = -120, 40
    for cy in range(20):
        for cx in range(30):
            gx = (cx * 8 + camx) // 8
            gy = (cy * 8 + camy) // 8
            rx, ry = gx >> 1, gy
            side = gx & 1
            if (rx + ry) % 2 == 0:
                orient = 0; ta = (rx, ry); tb = (rx + 1, ry + 1); xa = (rx + 1, ry - 1)
            else:
                orient = 1; ta = (rx + 1, ry); tb = (rx, ry + 1); xa = (rx, ry - 1)

            def w(p):
                a, b = p
                return T((a + b) // 2, (b - a) // 2)
            img[cy * 8:cy * 8 + 8, cx * 8:cx * 8 + 8] = tiles[(orient, side, w(ta), w(tb), 1 if w(xa) == WALL else 0)]
    save_preview("room_act%d" % act, img, ACT_BG[act - 1], 3)


for a in range(1, 6):
    preview_room(a)

# ---------------------------------------------------------------------------
# Sprite drawing helpers
SHADOW = 13


class Canvas:
    def __init__(self, w=32, h=32):
        self.a = np.zeros((h, w), dtype=np.uint8)

    def px(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < self.a.shape[1] and 0 <= y < self.a.shape[0]:
            self.a[y, x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(int(y0), int(y1) + 1):
            for x in range(int(x0), int(x1) + 1):
                self.px(x, y, c)

    def ell(self, cx, cy, rx, ry, c):
        for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
            for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
                if ((x - cx) / max(rx, 0.1)) ** 2 + ((y - cy) / max(ry, 0.1)) ** 2 <= 1.0:
                    self.px(x, y, c)

    def line(self, x0, y0, x1, y1, c, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / max(n, 1)
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t
            for dx in range(w):
                for dy in range(w):
                    self.px(round(x) + dx - w // 2, round(y) + dy - w // 2, c)

    def shadow(self, cx, cy, rx, ry=1.6):
        self.ell(cx, cy, rx, ry, SHADOW)

    def outline(self):
        a = self.a
        h, w = a.shape
        fig = (a != 0) & (a != SHADOW)
        out = a.copy()
        for y in range(h):
            for x in range(w):
                if fig[y, x]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and fig[ny, nx]:
                        out[y, x] = 1
                        break
        self.a = out
        return self

    def flip(self):
        self.a = self.a[:, ::-1].copy()
        return self


# ---------------------------------------------------------------------------
# PALADIN  (OBJ palette 0)
PAL_PLAYER = [(0, 0, 0), (18, 14, 16), (64, 72, 86), (128, 138, 152), (204, 210, 220), (138, 98, 30), (214, 172, 64),
              (230, 224, 204), (160, 150, 128), (176, 32, 30), (102, 16, 16), (222, 172, 132), (104, 66, 38),
              (14, 12, 14), (52, 84, 168), (16, 16, 30)]
P_OUT, P_STD, P_ST, P_STL, P_GD, P_G, P_W, P_WS, P_R, P_RD, P_SK, P_LE, P_SH, P_BL, P_VI = range(1, 16)
P_BL = 14


def pal_legs(c, x0, x1, top, bot, boot=True):
    c.rect(x0, top, x1, bot, P_ST)
    c.rect(x0, top, x0, bot, P_STL)
    if boot:
        c.rect(x0, bot - 1, x1, bot, P_LE)


def paladin(d, pose):
    """d: 0=S 1=SE 2=E 3=NE 4=N ; pose: 0 idle 1 walk-a 2 walk-b 3 attack"""
    c = Canvas()
    c.shadow(16, 30, 7)
    bob = 1 if pose in (1, 2) else 0
    back = d in (3, 4)
    side = d == 2
    sh = 1 if d in (1, 3) else 0          # 3/4 view shift
    # legs
    if side:
        if pose == 1:
            pal_legs(c, 12, 13, 24, 29); pal_legs(c, 17, 18, 24, 30)
        elif pose == 2:
            pal_legs(c, 13, 14, 24, 30); pal_legs(c, 16, 17, 24, 29)
        else:
            pal_legs(c, 14, 15, 24, 30); pal_legs(c, 16, 17, 24, 30)
    else:
        l_up = 1 if pose == 1 else 0
        r_up = 1 if pose == 2 else 0
        pal_legs(c, 13 + sh, 15 + sh, 24, 30 - l_up)
        pal_legs(c, 17 + sh, 19 + sh, 24, 30 - r_up)
    y0 = bob
    # sword arm / shield placement
    if back:
        # cape
        c.rect(11 + sh, 13 + y0, 21 + sh, 27, P_R)
        for fx in (13, 16, 19):
            c.line(fx + sh, 16 + y0, fx + sh, 27, P_RD)
        c.rect(11 + sh, 13 + y0, 21 + sh, 13 + y0, P_RD)
        # pauldrons
        c.rect(9 + sh, 12 + y0, 12 + sh, 14 + y0, P_G); c.rect(20 + sh, 12 + y0, 23 + sh, 14 + y0, P_G)
        # helmet back
        c.ell(16 + sh, 8 + y0, 3.5, 4, P_ST)
        c.rect(14 + sh, 5 + y0, 16 + sh, 6 + y0, P_STL)
        c.rect(15 + sh, 2 + y0, 16 + sh, 4 + y0, P_G)
        # shield on left (character's left is screen-left from behind)
        c.rect(6 + sh, 15 + y0, 10 + sh, 22 + y0, P_LE)
        c.rect(7 + sh, 23 + y0, 9 + sh, 24 + y0, P_LE)
        c.rect(6 + sh, 15 + y0, 10 + sh, 15 + y0, P_GD)
        # sword on right
        if pose == 3:
            c.line(22 + sh, 17 + y0, 30, 7, P_STL, 2)
            c.rect(20 + sh, 16 + y0, 23 + sh, 18 + y0, P_G)
        else:
            c.line(23 + sh, 6 + y0, 23 + sh, 19 + y0, P_STL, 2)
            c.rect(21 + sh, 19 + y0, 25 + sh, 19 + y0, P_G)
            c.rect(23 + sh, 20 + y0, 23 + sh, 21 + y0, P_LE)
    elif side:
        # torso (profile)
        c.rect(13, 13 + y0, 19, 23, P_ST)
        c.rect(13, 13 + y0, 13, 23, P_STD)
        c.rect(15, 14 + y0, 18, 23, P_W)
        c.rect(16, 16 + y0, 17, 20 + y0, P_R)
        c.rect(15, 17 + y0, 18, 18 + y0, P_R)
        c.rect(13, 22, 19, 22, P_LE)
        c.rect(12, 23, 19, 25, P_W)       # tabard skirt
        c.rect(12, 25, 19, 25, P_WS)
        c.rect(12, 12 + y0, 16, 14 + y0, P_G)   # pauldron
        # helmet profile
        c.ell(16, 8 + y0, 3.5, 4, P_ST)
        c.rect(17, 8 + y0, 19, 9 + y0, P_VI)
        c.rect(14, 5 + y0, 15, 6 + y0, P_STL)
        c.rect(15, 2 + y0, 16, 4 + y0, P_G)
        # shield held forward
        c.rect(19, 14 + y0, 21, 23 + y0, P_BL)
        c.rect(19, 14 + y0, 19, 23 + y0, P_G)
        c.rect(21, 14 + y0, 21, 23 + y0, P_G)
        c.rect(20, 17 + y0, 20, 20 + y0, P_W)
        if pose == 3:
            c.line(15, 18 + y0, 30, 16 + y0, P_STL, 2)
            c.rect(14, 15 + y0, 15, 20 + y0, P_G)
            c.rect(13, 17 + y0, 15, 19 + y0, P_ST)
        else:
            c.rect(12, 15 + y0, 13, 20 + y0, P_ST)   # sword arm (near)
            c.line(12, 4 + y0, 12, 18 + y0, P_STL, 2)
            c.rect(10, 18 + y0, 14, 18 + y0, P_G)
            c.rect(12, 19 + y0, 12, 20 + y0, P_LE)
    else:
        # front (S / SE)
        c.rect(12 + sh, 13 + y0, 20 + sh, 23, P_ST)
        c.rect(12 + sh, 13 + y0, 12 + sh, 23, P_STL)
        c.rect(20 + sh, 13 + y0, 20 + sh, 23, P_STD)
        c.rect(14 + sh, 14 + y0, 18 + sh, 22, P_W)
        c.rect(16 + sh, 15 + y0, 16 + sh, 20 + y0, P_R)
        c.rect(14 + sh, 17 + y0, 18 + sh, 17 + y0, P_R)
        c.rect(12 + sh, 22, 20 + sh, 22, P_LE)
        c.rect(16 + sh, 22, 16 + sh, 22, P_G)
        c.rect(12 + sh, 23, 20 + sh, 25, P_W)
        c.rect(12 + sh, 25, 20 + sh, 25, P_WS)
        c.rect(9 + sh, 12 + y0, 12 + sh, 14 + y0, P_G)
        c.rect(20 + sh, 12 + y0, 23 + sh, 14 + y0, P_G)
        c.rect(10 + sh, 15 + y0, 11 + sh, 21 + y0, P_ST)   # sword arm
        # head
        c.ell(16 + sh, 8 + y0, 3.5, 4, P_ST)
        c.rect(14 + sh, 9 + y0, 18 + sh, 9 + y0, P_VI)
        c.rect(16 + sh, 7 + y0, 16 + sh, 11 + y0, P_VI)
        c.rect(14 + sh, 5 + y0, 15 + sh, 6 + y0, P_STL)
        c.rect(15 + sh, 2 + y0, 16 + sh, 4 + y0, P_G)
        # kite shield on screen-right
        sx = 21 + sh + (1 if d == 1 else 0)
        c.rect(sx, 14 + y0, sx + 5, 21 + y0, P_BL)
        c.rect(sx + 1, 22 + y0, sx + 4, 23 + y0, P_BL)
        c.rect(sx + 2, 24 + y0, sx + 3, 24 + y0, P_BL)
        c.rect(sx, 14 + y0, sx + 5, 14 + y0, P_G)
        c.rect(sx, 14 + y0, sx, 21 + y0, P_G)
        c.rect(sx + 5, 14 + y0, sx + 5, 21 + y0, P_GD)
        c.rect(sx + 2, 16 + y0, sx + 3, 21 + y0, P_W)
        c.rect(sx + 1, 17 + y0, sx + 4, 18 + y0, P_W)
        if pose == 3:
            c.line(10 + sh, 20 + y0, 1, 26, P_STL, 2)
            c.rect(8 + sh, 19 + y0, 11 + sh, 21 + y0, P_G)
        else:
            c.line(9 + sh, 5 + y0, 9 + sh, 18 + y0, P_STL, 2)
            c.rect(7 + sh, 19 + y0, 11 + sh, 19 + y0, P_G)
            c.rect(9 + sh, 20 + y0, 9 + sh, 21 + y0, P_LE)
    return c.outline().a


player_words = []
sheet = np.zeros((32 * 5, 32 * 4), dtype=np.uint8)
for d in range(5):
    for pose in range(4):
        a = paladin(d, pose)
        player_words += tiles_from_img(a)
        sheet[d * 32:d * 32 + 32, pose * 32:pose * 32 + 32] = a
emit_words("spr_player", player_words)
emit_pal("pal_player", PAL_PLAYER)
save_preview("player", sheet, PAL_PLAYER, 4)

# ---------------------------------------------------------------------------
# MONSTERS (archetypes drawn facing right)
# slots: 1 outline 2-4 body d/m/l 5-7 accent d/m/l 8 eye 9 bone/teeth 10-11 metal d/l 12 extra 13 shadow


def m_imp(pose):
    c = Canvas(); c.shadow(16, 30, 6)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    c.rect(13 - st, 24, 14 - st, 29, 2); c.rect(17 + st, 24, 18 + st, 29, 3)
    c.line(12, 22, 8, 20, 2); c.px(7, 19, 2)                # tail
    c.ell(15.5, 21, 4, 4, 3); c.ell(16, 22, 2, 2.5, 4)      # body
    c.ell(18, 15, 3.5, 3.5, 3)                              # head
    c.px(20, 14, 8); c.px(19, 14, 8)
    c.rect(19, 17, 21, 17, 9)
    c.line(16, 12, 15, 9, 5); c.line(19, 12, 20, 9, 5)        # horns
    c.rect(17, 19, 19, 20, 2)                               # arm
    if pose == 3:
        c.line(20, 20, 28, 25, 6, 2); c.rect(27, 24, 29, 26, 5)
    else:
        c.line(20, 19, 25, 12, 6, 2); c.rect(24, 11, 26, 13, 5)
    return c.outline().a


def m_skeleton(pose):
    c = Canvas(); c.shadow(16, 30, 6)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    c.line(14, 22, 13 - st, 29, 3); c.line(17, 22, 18 + st, 29, 3)
    c.rect(12 - st, 29, 14 - st, 29, 4); c.rect(17 + st, 29, 19 + st, 29, 4)
    c.rect(13, 21, 18, 22, 3)
    c.rect(15, 13, 16, 21, 3)
    for ry in (14, 16, 18):
        c.rect(13, ry, 18, ry, 9)
    c.ell(16.5, 9, 3.5, 3.5, 9); c.rect(14, 11, 19, 12, 4)
    c.px(18, 9, 1); c.px(18, 9, 8); c.px(15, 9, 1)
    c.rect(15, 12, 18, 12, 2)
    c.ell(11, 17, 2.5, 3.5, 6); c.px(11, 17, 7)               # round shield
    if pose == 3:
        c.line(18, 16, 22, 18, 3); c.line(22, 18, 30, 18, 11, 2); c.rect(21, 16, 21, 20, 10)
    else:
        c.line(18, 15, 20, 17, 3); c.line(21, 16, 26, 7, 11, 2); c.rect(19, 16, 23, 16, 10)
    return c.outline().a


def m_zombie(pose):
    c = Canvas(); c.shadow(16, 30, 6)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    c.rect(13 - st, 22, 15 - st, 29, 5); c.rect(16 + st, 22, 18 + st, 29, 6)
    c.rect(12, 13, 18, 22, 3); c.rect(12, 13, 12, 22, 2); c.rect(17, 14, 18, 22, 4)
    c.rect(12, 19, 18, 20, 5); c.line(13, 15, 17, 17, 6)      # rags
    c.ell(17, 10, 3, 3.5, 3); c.px(19, 9, 8); c.rect(18, 12, 20, 12, 1)
    if pose == 3:
        c.line(17, 14, 25, 10, 3, 2); c.line(17, 16, 26, 13, 4, 2)
    else:
        c.line(17, 14, 24, 15, 3, 2); c.line(17, 16, 24, 17, 4, 2)
    return c.outline().a


def m_beast(pose):
    c = Canvas(); c.shadow(16, 30, 10)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    for lx, s in ((9, st), (12, -st), (19, -st), (22, st)):
        c.rect(lx + s, 22, lx + 1 + s, 29, 2)
    c.ell(15, 19, 8.5, 4.5, 3); c.rect(9, 15, 21, 16, 4)      # body, fur top
    c.line(7, 17, 3, 13, 3, 2)                                # tail
    head_y = 14 if pose != 3 else 16
    c.ell(24, head_y, 3.5, 3.5, 3)
    c.rect(26, head_y, 29, head_y + 2, 3)                     # snout
    c.px(29, head_y, 1)
    c.line(22, head_y - 3, 23, head_y - 6, 5); c.line(24, head_y - 3, 26, head_y - 5, 5)  # ears
    c.px(25, head_y - 1, 8)
    if pose == 3:
        c.rect(26, head_y + 3, 29, head_y + 3, 9); c.px(27, head_y + 2, 1)
    return c.outline().a


def m_spider(pose):
    c = Canvas(); c.shadow(16, 30, 11)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    for i, (bx, fx) in enumerate(((14, 7), (16, 12), (18, 21), (20, 26))):
        s = st if i % 2 else -st
        c.line(bx, 20, bx - 2 + (fx - bx) // 2, 13 + s, 2)
        c.line(bx - 2 + (fx - bx) // 2, 13 + s, fx + s, 28, 2)
    c.ell(11, 19, 6, 5, 3); c.ell(10, 17, 3, 2, 4); c.rect(9, 20, 13, 21, 6)  # abdomen
    c.ell(19, 21, 4, 3, 3)
    c.px(22, 19, 8); c.px(21, 20, 8); c.px(23, 20, 8)
    if pose == 3:
        c.line(22, 22, 25, 26, 9); c.line(21, 23, 23, 27, 9)
    else:
        c.line(22, 22, 24, 24, 9)
    return c.outline().a


def m_demon(pose):
    c = Canvas(); c.shadow(16, 30, 10, 1.5)
    st = 1 if pose == 1 else (-1 if pose == 2 else 0)
    # wings
    c.line(10, 10, 1, 3, 5, 2); c.line(1, 3, 3, 18, 5); c.line(10, 10, 3, 18, 6)
    c.line(22, 10, 31, 3, 5, 2); c.line(31, 3, 29, 18, 5); c.line(22, 10, 29, 18, 6)
    for k in range(4, 17, 3):
        c.line(10, 11, 2 + k // 6, k, 6); c.line(22, 11, 30 - k // 6, k, 6)
    c.rect(11 - st, 22, 14 - st, 29, 2); c.rect(18 + st, 22, 21 + st, 29, 2)
    c.rect(10 - st, 29, 14 - st, 29, 9); c.rect(18 + st, 29, 22 + st, 29, 9)
    c.ell(16, 16, 7, 7, 3); c.ell(13.5, 14, 2, 2, 4); c.ell(18.5, 14, 2, 2, 4)
    c.rect(12, 19, 20, 21, 2)
    c.ell(16, 7, 3.5, 3.5, 3)
    c.line(13, 6, 9, 1, 9); c.line(19, 6, 23, 1, 9)
    c.px(15, 7, 8); c.px(17, 7, 8); c.rect(15, 9, 17, 9, 12)
    if pose == 3:
        c.line(10, 13, 6, 3, 3, 2); c.line(22, 13, 26, 3, 3, 2)
        c.rect(5, 1, 7, 3, 9); c.rect(25, 1, 27, 3, 9)
    else:
        c.line(9, 12, 7, 22, 3, 2); c.line(23, 12, 25, 22, 3, 2)
        c.rect(6, 22, 8, 24, 9); c.rect(24, 22, 26, 24, 9)
    return c.outline().a


ARCH = [m_imp, m_skeleton, m_zombie, m_beast, m_spider]
mon_words = []
msheet = np.zeros((32 * 5, 32 * 3), dtype=np.uint8)
for ai, fn in enumerate(ARCH):
    for pose in range(3):
        a = fn((1, 2, 3)[pose])
        mon_words += tiles_from_img(a)
        msheet[ai * 32:ai * 32 + 32, pose * 32:pose * 32 + 32] = a
emit_words("spr_monsters", mon_words)

boss_words = []
bsheet = np.zeros((64, 64 * 3), dtype=np.uint8)
for pose in range(3):
    a = m_demon((1, 2, 3)[pose])
    big = np.kron(a, np.ones((2, 2), dtype=np.uint8))
    boss_words += tiles_from_img(big)
    bsheet[:, pose * 64:pose * 64 + 64] = big
emit_words("spr_boss", boss_words)


def mpal(body, accent, eye=(255, 220, 60), bone=(232, 226, 210), metal=(150, 156, 170), extra=(255, 120, 40)):
    def sh(c, f):
        return tuple(max(0, min(255, int(v * f))) for v in c)
    return [(0, 0, 0), (16, 12, 14), sh(body, 0.62), body, sh(body, 1.3), sh(accent, 0.65), accent, sh(accent, 1.3),
            eye, bone, sh(metal, 0.6), metal, extra, (12, 10, 12), (0, 0, 0), (0, 0, 0)]


# Per act: 3 monsters (archetype, palette) + boss palette
ACT_MON = [
    [(0, mpal((168, 44, 36), (108, 70, 40))), (1, mpal((206, 198, 176), (90, 70, 120))), (2, mpal((100, 128, 80), (92, 70, 50)))],
    [(4, mpal((186, 150, 80), (120, 80, 40))), (2, mpal((206, 186, 140), (150, 124, 84), eye=(120, 220, 255))), (3, mpal((170, 124, 70), (100, 70, 40)))],
    [(0, mpal((80, 140, 60), (100, 70, 40))), (4, mpal((110, 60, 130), (200, 180, 60))), (3, mpal((60, 90, 56), (40, 60, 40)))],
    [(0, mpal((230, 120, 40), (90, 40, 30), eye=(255, 255, 160))), (1, mpal((96, 40, 40), (150, 30, 30), eye=(255, 80, 40))), (3, mpal((190, 60, 30), (255, 160, 40)))],
    [(0, mpal((80, 170, 210), (60, 80, 120), eye=(255, 255, 255))), (1, mpal((170, 200, 230), (70, 100, 150), eye=(120, 220, 255))), (3, mpal((226, 230, 236), (150, 160, 180), eye=(80, 180, 255)))],
]
BOSS_PAL = [
    mpal((150, 30, 50), (90, 30, 60), extra=(255, 255, 255)),
    mpal((170, 130, 60), (110, 80, 30), eye=(120, 255, 200)),
    mpal((60, 90, 70), (40, 50, 40), eye=(220, 255, 80)),
    mpal((180, 40, 20), (240, 120, 30), eye=(255, 255, 120)),
    mpal((180, 190, 210), (90, 110, 150), eye=(120, 220, 255)),
]
C.append("const unsigned char act_mon_arch[5][3] = {%s};\n" % ",".join(
    "{" + ",".join(str(m[0]) for m in a) + "}" for a in ACT_MON))
H.append("extern const unsigned char act_mon_arch[5][3];\n")
emit_pal_array("pal_act_mon", [m[1] for a in ACT_MON for m in a])
emit_pal_array("pal_boss", BOSS_PAL)
save_preview("monsters_act1", msheet, ACT_MON[0][0][1], 4)
save_preview("boss_act1", bsheet, BOSS_PAL[0], 3)

# ---------------------------------------------------------------------------
# NPC healer (robed elder), portal, campfire, items, effects
PAL_MISC = [(0, 0, 0), (16, 12, 14), (40, 60, 120), (70, 100, 180), (130, 160, 220), (90, 60, 34), (140, 100, 60),
            (226, 176, 136), (240, 240, 230), (255, 220, 90), (200, 120, 30), (255, 250, 200), (180, 30, 30),
            (12, 10, 12), (110, 200, 255), (60, 140, 230)]


def npc(frame):
    c = Canvas(); c.shadow(16, 30, 6)
    c.rect(12, 14, 20, 29, 3); c.rect(12, 14, 13, 29, 2); c.rect(18, 15, 20, 29, 4)
    c.rect(15, 15, 16, 29, 8)
    c.ell(16, 10, 4, 4, 3); c.ell(16, 11, 2.5, 2.5, 7); c.px(15, 11, 1); c.px(17, 11, 1)
    c.rect(15, 13, 17, 14, 8)  # beard
    c.line(22, 6, 22, 29, 6); c.rect(21, 13, 23, 15, 3)
    g = 11 if frame else 9
    c.ell(22, 4, 2, 2, g)
    return c.outline().a


def portal(frame):
    c = Canvas()
    c.ell(16, 16, 7, 13, 2)
    c.ell(16, 16, 5.5, 11, 15)
    c.ell(16, 16, 3.5, 8, 14)
    for k in range(6):
        ang = k * math.pi / 3 + frame * 0.5
        c.px(16 + 3 * math.cos(ang), 16 + 7 * math.sin(ang), 8)
        c.px(16 + 5 * math.cos(ang + .4), 16 + 10 * math.sin(ang + .4), 4)
    c.shadow(16, 30, 7)
    return c.outline().a


def fire(frame):
    c = Canvas(16, 16)
    c.line(3, 13, 12, 11, 5, 2); c.line(4, 11, 12, 13, 6, 2)
    f = [(8, 4), (6, 6), (10, 6)] if frame else [(7, 3), (9, 5), (5, 7)]
    c.ell(8, 9, 3, 3.5, 10); c.ell(8, 9, 1.5, 2, 9)
    for (x, y) in f:
        c.px(x, y, 9); c.px(x, y + 1, 10)
    return c.outline().a


def item(k):
    c = Canvas(16, 16)
    if k in (0, 1):     # potions
        col, lt = (12, 9) if k == 0 else (15, 14)
        c.ell(8, 10, 4, 4, col); c.px(6, 8, lt); c.px(7, 8, lt)
        c.rect(7, 3, 9, 6, 4); c.rect(7, 2, 9, 2, 6)
    elif k == 2:        # gold
        for (x, y) in ((5, 11), (9, 11), (7, 9), (11, 12), (6, 13)):
            c.ell(x, y, 2, 1.3, 9); c.px(x - 1, y, 11)
    elif k == 3:        # sword
        c.line(3, 13, 12, 4, 4, 2); c.line(2, 10, 6, 14, 9, 1); c.px(2, 14, 6)
    elif k == 4:        # armor
        c.rect(4, 4, 11, 12, 3); c.rect(4, 4, 11, 5, 9); c.rect(2, 4, 4, 7, 3); c.rect(11, 4, 13, 7, 3)
        c.rect(7, 6, 8, 11, 4)
    return c.outline().a


def fx(k, frame):
    c = Canvas(16, 16)
    if k == 0:   # holy bolt
        c.ell(8, 8, 4, 4, 9); c.ell(8, 8, 2, 2, 11)
        for i in range(8):
            ang = i * math.pi / 4 + frame * math.pi / 8
            c.px(8 + 6 * math.cos(ang), 8 + 6 * math.sin(ang), 11 if i % 2 else 9)
    elif k == 1:  # spinning hammer
        if frame:
            c.line(3, 3, 12, 12, 6, 2); c.rect(10, 2, 14, 6, 4); c.rect(11, 3, 13, 5, 8)
        else:
            c.line(3, 12, 12, 3, 6, 2); c.rect(10, 9, 14, 13, 4); c.rect(11, 10, 13, 12, 8)
        c.ell(8, 8, 7, 7, 0) if False else None
    elif k == 2:  # hit spark
        r = 3 if frame == 0 else 6
        for i in range(8):
            ang = i * math.pi / 4 + (0.4 if frame else 0)
            c.line(8 + 1 * math.cos(ang), 8 + 1 * math.sin(ang), 8 + r * math.cos(ang), 8 + r * math.sin(ang), 11 if i % 2 else 9)
    return c.outline().a if k != 2 else c.a


misc_words = []
misc_sheet = []
for f in range(2):
    a = npc(f); misc_words += tiles_from_img(a); misc_sheet.append(a)
for f in range(2):
    a = portal(f); misc_words += tiles_from_img(a); misc_sheet.append(a)
small = []
for f in range(2):
    small.append(fire(f))
for k in range(5):
    small.append(item(k))
for k in range(3):
    for f in range(2):
        small.append(fx(k, f))
small_words = []
for a in small:
    small_words += tiles_from_img(a)
emit_words("spr_big_misc", misc_words)      # npc0,npc1,portal0,portal1 (32x32 each)
emit_words("spr_small_misc", small_words)    # fire0,fire1,potR,potB,gold,sword,armor,bolt0,bolt1,ham0,ham1,spark0,spark1
emit_pal("pal_misc", PAL_MISC)
row = np.zeros((32, 32 * 4), dtype=np.uint8)
for i, a in enumerate(misc_sheet):
    row[:, i * 32:i * 32 + 32] = a
save_preview("misc_big", row, PAL_MISC, 4)
row = np.zeros((16, 16 * len(small)), dtype=np.uint8)
for i, a in enumerate(small):
    row[:, i * 16:i * 16 + 16] = a
save_preview("misc_small", row, PAL_MISC, 4)

# ---------------------------------------------------------------------------
# FONT (5x7, original) -> 8x8 tiles with drop shadow. Index 1 = text, 2 = shadow
GLYPHS = {
    'A': [".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    'B': ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    'C': [".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."],
    'D': ["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."],
    'E': ["#####", "#....", "#....", "####.", "#....", "#....", "#####"],
    'F': ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    'G': [".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"],
    'H': ["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
    'I': [".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    'J': ["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."],
    'K': ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    'L': ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    'M': ["#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"],
    'N': ["#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#"],
    'O': [".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    'P': ["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."],
    'Q': [".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"],
    'R': ["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"],
    'S': [".####", "#....", "#....", ".###.", "....#", "....#", "####."],
    'T': ["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."],
    'U': ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."],
    'V': ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    'W': ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#."],
    'X': ["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"],
    'Y': ["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."],
    'Z': ["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"],
    '0': [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    '1': ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    '2': [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    '3': ["####.", "....#", "....#", ".###.", "....#", "....#", "####."],
    '4': ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    '5': ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    '6': ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    '7': ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    '8': [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    '9': [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
    '.': [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
    ',': [".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."],
    ':': [".....", ".##..", ".##..", ".....", ".##..", ".##..", "....."],
    '!': ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    '?': [".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."],
    '-': [".....", ".....", ".....", ".###.", ".....", ".....", "....."],
    '+': [".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."],
    '/': ["....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."],
    "'": ["..#..", "..#..", ".#...", ".....", ".....", ".....", "....."],
    '(': ["...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#."],
    ')': [".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#..."],
    '%': ["##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##"],
    '>': [".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#..."],
    '<': ["...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#."],
    '=': [".....", ".....", "#####", ".....", "#####", ".....", "....."],
    '*': [".....", ".#.#.", "#####", "#####", ".###.", "..#..", "....."],   # heart
    '#': ["..#..", ".###.", "#####", "#####", ".###.", "..#..", "....."],   # mana gem
    '$': [".....", ".###.", "#####", "##.##", "#####", ".###.", "....."],   # coin
    '_': [".....", ".....", ".....", ".....", ".....", ".....", "#####"],
    '^': ["..#..", ".###.", "#.#.#", "..#..", "..#..", "..#..", "....."],   # up arrow
}
FONT_CHARS = " " + "".join(chr(c) for c in range(33, 96))
font_words = []
fsheet = np.zeros((8 * 4, 8 * 16), dtype=np.uint8)
for i, ch in enumerate(FONT_CHARS):
    a = np.zeros((8, 8), dtype=np.uint8)
    g = GLYPHS.get(ch)
    if g:
        for y, row in enumerate(g):
            for x, v in enumerate(row):
                if v == '#':
                    if a[y + 1, x + 2] == 0:
                        a[y + 1, x + 2] = 2
        for y, row in enumerate(g):
            for x, v in enumerate(row):
                if v == '#':
                    a[y, x + 1] = 1
    font_words += tiles_from_img(a)
    fsheet[(i // 16) * 8:(i // 16) * 8 + 8, (i % 16) * 8:(i % 16) * 8 + 8] = a
# boxed variant: background filled with index 3 (menu box colour)
for i, ch in enumerate(FONT_CHARS):
    a = np.full((8, 8), 3, dtype=np.uint8)
    g = GLYPHS.get(ch)
    if g:
        for y, row in enumerate(g):
            for x, v in enumerate(row):
                if v == '#' and a[y + 1, x + 2] == 3:
                    a[y + 1, x + 2] = 2
        for y, row in enumerate(g):
            for x, v in enumerate(row):
                if v == '#':
                    a[y, x + 1] = 1
    font_words += tiles_from_img(a)
emit_words("font_tiles", font_words)
C.append("const int font_count = %d;\n" % len(FONT_CHARS))
H.append("extern const int font_count;\n")
FONT_COLS = [(240, 236, 224), (230, 60, 50), (220, 180, 80), (110, 130, 255), (150, 150, 150), (255, 255, 120), (110, 220, 110), (255, 255, 255)]
emit_pal_array("pal_font", [[(0, 0, 0), c, (8, 6, 6), (20, 16, 14)] for c in FONT_COLS])
save_preview("font", fsheet, [(0, 0, 0), (255, 255, 255), (60, 60, 60)], 3)

# ---------------------------------------------------------------------------
# HUD tiles (palette bank 9): bar fills red/gold/blue 0..8 px + panel tiles
PAL_HUD = [(0, 0, 0), (10, 8, 8), (34, 28, 26), (150, 20, 20), (230, 70, 60), (170, 130, 40), (240, 200, 90),
           (30, 50, 170), (90, 130, 250), (48, 38, 32), (88, 70, 52), (130, 104, 70), (20, 16, 14)]
hud = []
for col in ((3, 4), (5, 6), (7, 8)):
    for f in range(9):
        a = np.full((8, 8), 12, dtype=np.uint8)
        a[1, :] = 1; a[6, :] = 1
        for y in range(2, 6):
            for x in range(8):
                a[y, x] = (col[1] if y == 2 else col[0]) if x < f else 2
        hud.append(a)
# panel: plain, top border
a = np.full((8, 8), 9, dtype=np.uint8); a[0, :] = 11; a[1, :] = 10; hud.append(a)   # 27 panel top
a = np.full((8, 8), 9, dtype=np.uint8)
for x in range(8):
    if (x + 1) % 4 == 0:
        a[3, x] = 10
hud.append(a)                                                                        # 28 panel plain
# menu box tiles: 29 fill, 30 top, 31 bottom, 32 left, 33 right, 34-37 corners
fill = np.full((8, 8), 12, dtype=np.uint8)
hud.append(fill)
t = fill.copy(); t[0, :] = 11; t[1, :] = 10; hud.append(t)
t = fill.copy(); t[7, :] = 11; t[6, :] = 10; hud.append(t)
t = fill.copy(); t[:, 0] = 11; t[:, 1] = 10; hud.append(t)
t = fill.copy(); t[:, 7] = 11; t[:, 6] = 10; hud.append(t)
for corner in range(4):
    t = fill.copy()
    if corner < 2: t[0, :] = 11; t[1, :] = 10
    else: t[7, :] = 11; t[6, :] = 10
    if corner % 2 == 0: t[:, 0] = 11; t[:, 1] = 10
    else: t[:, 7] = 11; t[:, 6] = 10
    t[1 if corner < 2 else 6, 1 if corner % 2 == 0 else 6] = 6
    hud.append(t)
hud_words = []
for a in hud:
    hud_words += tiles_from_img(a)
emit_words("hud_tiles", hud_words)
emit_pal("pal_hud", PAL_HUD)

# ---------------------------------------------------------------------------
# TITLE SCREEN (BG, palette bank 10), unique-tile compressed
W, Hh = 240, 160
img = Image.new("RGB", (W, Hh), (0, 0, 0))
dr = ImageDraw.Draw(img)
for y in range(Hh):
    for x in range(W):
        d = math.hypot((x - 120) / 120, (y - 70) / 90)
        v = max(0.0, 1.0 - d)
        n = (h32(x, y) % 100) / 100.0
        img.putpixel((x, y), (int(70 * v + 8 * n), int(10 * v + 3 * n), int(14 * v + 4 * n)))
# cross / sword silhouette
dr.rectangle([116, 18, 124, 118], fill=(26, 20, 22))
dr.rectangle([96, 44, 144, 52], fill=(26, 20, 22))
dr.polygon([(116, 118), (124, 118), (120, 130)], fill=(26, 20, 22))
fontpath = "/usr/share/texmf/fonts/opentype/public/tex-gyre/texgyrebonum-bold.otf"
if not os.path.exists(fontpath):
    fontpath = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
f1 = ImageFont.truetype(fontpath, 34)
f2 = ImageFont.truetype(fontpath, 26)


def title_text(txt, font, y, fill):
    bb = dr.textbbox((0, 0), txt, font=font)
    x = (W - (bb[2] - bb[0])) // 2 - bb[0]
    for dx in (-2, -1, 0, 1, 2):
        for dy in (-2, -1, 0, 1, 2):
            if dx or dy:
                dr.text((x + dx, y + dy), txt, font=font, fill=(10, 4, 4))
    dr.text((x, y), txt, font=font, fill=fill)


title_text("ABYSS", f1, 40, (222, 176, 76))
title_text("PALADIN", f2, 78, (196, 196, 206))
img = img.convert("RGB")
q = img.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
qp = q.getpalette()[:45]
tpal = [(0, 0, 0)] + [tuple(qp[i * 3:i * 3 + 3]) for i in range(15)]
qa = np.array(q, dtype=np.uint8) + 1
tiles = {}
tlist = []
tmap = []
for ty in range(20):
    for tx in range(30):
        t = qa[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
        key = t.tobytes()
        if key not in tiles:
            tiles[key] = len(tlist)
            tlist.append(t.copy())
        tmap.append(tiles[key])
tw = []
for t in tlist:
    tw += tiles_from_img(t)
emit_words("title_tiles", tw)
C.append("const unsigned short title_map[600] = {%s};\n" % ",".join(str(v) for v in tmap))
H.append("extern const unsigned short title_map[600];\n")
C.append("const int title_tile_count = %d;\n" % len(tlist))
H.append("extern const int title_tile_count;\n")
emit_pal("pal_title", tpal)
Image.fromarray(np.array(img)).resize((480, 320), Image.NEAREST).save(os.path.join(PREVIEW, "title.png"))
print("title tiles:", len(tlist))

sin_vals = [int(round(math.sin(i * 2 * math.pi / 256) * 256)) for i in range(256)]
C.append("const short sin_lut[256] = {%s};\n" % ",".join(str(v) for v in sin_vals))
H.append("extern const short sin_lut[256];\n")

with open(os.path.join(OUT, "assets.c"), "w") as f:
    f.write("// Generated by tools/gen_assets.py - original art for ABYSS PALADIN\n#include \"assets.h\"\n")
    f.write("".join(C))
with open(os.path.join(OUT, "assets.h"), "w") as f:
    f.write("#ifndef ASSETS_H\n#define ASSETS_H\n" + "".join(H) + "#endif\n")
print("ok")
