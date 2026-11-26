#!/usr/bin/env python3
"""Pre-rendered 3D paladin for ABYSS PALADIN.

An original knight model is built from simple 3D shapes (signed distance functions), posed, and
ray-marched from the game's isometric camera in 8 true directions (no mirroring, so the sword stays
in the right hand and the shield in the left).  Each frame is rendered at 4x size, reduced to a
32x32 pixel-art sprite in the player's 16-colour palette, and outlined.
Only numpy is needed.  render_frame(direction, pose) -> 32x32 uint8 palette indices.
"""
import numpy as np

SS = 4                      # supersampling factor
SIZE = 32
ANCHOR = (16.0, 29.0)       # screen position of the feet inside the 32x32 sprite
ELEV = np.radians(30.0)     # 2:1 isometric camera

# palette slots (see PAL_PLAYER in gen_assets.py)
OUT, STD, ST, STL, GD, G, W, WS, R, RD, SK, LE, SHADOW, BL, VI = range(1, 16)
RAMP = {
    1: [STD, ST, ST, STL],      # steel
    2: [GD, G, G, W],           # gold
    3: [WS, W, W, W],           # white tabard
    4: [RD, R, R, R],           # red cloth
    5: [SHADOW, LE, LE, GD],    # leather
    6: [VI, BL, BL, STL],       # blue shield
    7: [VI, VI, VI, VI],        # visor slit
    8: [STD, STL, STL, W],      # blade (bright steel)
}

POSES = ["idle", "walk_a", "walk_b", "windup", "strike"]


# ---------------------------------------------------------------- SDF primitives
def sd_sphere(p, c, r):
    return np.linalg.norm(p - c, axis=-1) - r


def sd_capsule(p, a, b, r):
    pa, ba = p - a, b - a
    h = np.clip((pa @ ba) / (ba @ ba), 0, 1)
    return np.linalg.norm(pa - h[..., None] * ba, axis=-1) - r


def sd_box(p, c, half, rad=0.0):
    q = np.abs(p - c) - (np.asarray(half) - rad)
    return np.linalg.norm(np.maximum(q, 0), axis=-1) + np.minimum(np.max(q, axis=-1), 0) - rad


def sd_ellipsoid(p, c, r):
    q = (p - c) / r
    k0 = np.linalg.norm(q, axis=-1)
    k1 = np.linalg.norm(q / r, axis=-1)
    return k0 * (k0 - 1) / np.maximum(k1, 1e-6)


def normalize(v):
    v = np.asarray(v, dtype=float)
    return v / np.linalg.norm(v)


# ---------------------------------------------------------------- the model
def build(pose):
    """returns list of (sdf_fn(p_model) -> dist, material, detail_fn or None)"""
    V = np.array
    bob = {"idle": 0.0, "walk_a": 0.6, "walk_b": 0.6, "windup": 0.0, "strike": -0.6}[pose]
    step = {"walk_a": 2.4, "walk_b": -2.4}.get(pose, 0.0)
    lunge = 1.4 if pose == "strike" else 0.0
    parts = []

    def add(fn, mat, detail=None):
        parts.append((fn, mat, detail))

    # legs (steel greaves) + boots
    for side, s in ((1, step), (-1, -step)):
        if pose == "strike":
            s = 2.2 if side == -1 else -1.6      # lunge: left leg forward
        hip = V([1.9 * side, 0.2 + lunge * 0.4, 9.6 + bob])
        knee = V([2.1 * side, s * 0.55 + 0.4, 5.4 + bob * 0.5 + abs(s) * 0.15])
        foot = V([2.2 * side, s, 1.4 + (0.5 if s > 0.5 else 0.0)])
        add(lambda p, a=hip, b=knee: sd_capsule(p, a, b, 1.55), 1)
        add(lambda p, a=knee, b=foot: sd_capsule(p, a, b, 1.4), 1)
        add(lambda p, c=foot: sd_box(p, c + V([0, 0.9, -0.5]), [1.3, 2.0, 0.9], 0.5), 5)

    z0 = bob
    lean = 0.8 if pose == "strike" else (-0.4 if pose == "windup" else 0.0)
    # torso (breastplate) with a slight forward lean
    def torso(p):
        q = p.copy()
        q[..., 1] -= lean * (q[..., 2] - 9) / 8
        return sd_box(q, V([0, 0.2, 14.2 + z0]), [3.5, 2.2, 3.8], 1.3)
    add(torso, 1)
    # tabard over the chest and skirt: white with a red cross on the front
    def tabard(p):
        q = p.copy()
        q[..., 1] -= lean * (q[..., 2] - 9) / 8
        top = sd_box(q, V([0, 0.45, 13.6 + z0]), [2.7, 2.2, 3.6], 0.6)
        skirt = sd_box(q, V([0, 0.3, 8.6 + z0 * 0.5]), [3.3, 2.3, 1.9], 0.7)
        return np.minimum(top, skirt)

    def cross(p, mat):
        on = (p[..., 1] > 1.6) & ((np.abs(p[..., 0]) < 0.75) & (np.abs(p[..., 2] - 13.4 - z0) < 3.0) |
                                  (np.abs(p[..., 2] - 14.4 - z0) < 0.75) & (np.abs(p[..., 0]) < 2.1))
        return np.where(on, 4, mat)
    add(tabard, 3, cross)
    # belt
    add(lambda p: sd_box(p, V([0, 0.3, 10.4 + z0]), [3.6, 2.5, 0.55], 0.4), 5)
    # cape at the back
    def cape(p):
        z = p[..., 2]
        flare = np.clip((18 + z0 - z) / 11, 0, 1)
        q = p.copy()
        q[..., 1] += 2.5 + flare * (1.2 + (0.8 if pose in ("walk_a", "walk_b") else 0))
        half = np.stack([3.4 + flare * 1.0, np.full_like(flare, 0.45), np.full_like(flare, 5.6)], -1)
        return sd_box(q, V([0, 0, 12.2 + z0]), half, 0.3)
    add(cape, 4)
    # helmet + visor + crest
    head = V([0, 0.3 + lean * 0.6, 19.8 + z0])
    add(lambda p: sd_sphere(p, head, 2.9), 1,
        lambda p, m: np.where((p[..., 1] - head[1] > 1.4) & (np.abs(p[..., 2] - head[2] - 0.2) < 0.45) |
                              (p[..., 1] - head[1] > 1.9) & (np.abs(p[..., 0]) < 0.4) &
                              (p[..., 2] - head[2] < 0.3) & (p[..., 2] - head[2] > -1.8), 7, m))
    add(lambda p: sd_box(p, head + V([0, -0.4, 2.7]), [0.45, 2.0, 0.8], 0.35), 2)
    # pauldrons
    for side in (1, -1):
        add(lambda p, c=V([4.1 * side, 0.2, 16.6 + z0]): sd_ellipsoid(p, c, np.array([1.8, 2.0, 1.4])), 2)

    # ---- right arm + sword
    sh_r = V([4.2, 0.2, 16.0 + z0])
    if pose == "windup":
        hand = V([3.6, -1.6, 19.5 + z0]); blade = normalize([0.25, -0.75, 0.6])
    elif pose == "strike":
        hand = V([2.6, 5.0, 12.8 + z0]); blade = normalize([-0.35, 0.92, -0.18])
    else:
        sw = {"walk_a": -0.8, "walk_b": 0.8}.get(pose, 0.0)
        hand = V([5.3, 1.8 + sw, 11.0 + z0]); blade = normalize([0.42, 0.4, 0.82])
    elbow = (sh_r + hand) / 2 + V([1.0, -0.5, 0.0])
    add(lambda p: sd_capsule(p, sh_r, elbow, 1.15), 1)
    add(lambda p: sd_capsule(p, elbow, hand, 1.05), 1)
    add(lambda p: sd_sphere(p, hand, 1.05), 5)
    tip = hand + blade * 10.5
    guard_dir = normalize(np.cross(blade, [0, 0, 1]) if abs(blade[2]) < 0.95 else [1, 0, 0])
    add(lambda p: sd_capsule(p, hand + blade * 0.9, tip, 0.5), 8)
    add(lambda p: sd_capsule(p, hand + blade * 0.9 - guard_dir * 1.6, hand + blade * 0.9 + guard_dir * 1.6, 0.45), 2)
    add(lambda p: sd_capsule(p, hand - blade * 1.2, hand, 0.45), 5)

    # ---- left arm + kite shield
    sh_l = V([-4.2, 0.2, 16.0 + z0])
    sw = {"walk_a": 0.6, "walk_b": -0.6}.get(pose, 0.0)
    sc = V([-4.9, 2.1 + sw + (1.0 if pose == "strike" else 0), 12.8 + z0])
    add(lambda p: sd_capsule(p, sh_l, sc + V([1.0, -1.2, 0.5]), 1.1), 1)

    def shield(p):
        q = p - sc
        # kite outline: wide top, pointed bottom; thin along x (faces forward-left)
        y, z = q[..., 1], q[..., 2]
        halfw = np.where(z > 0, 2.6, 2.6 * np.clip(1 + z / 4.6, 0, 1))
        dy = np.abs(y) - halfw
        dz = np.maximum(z - 3.2, -4.6 - z)
        d2 = np.maximum(dy, dz)
        return np.maximum(d2, np.abs(q[..., 0]) - 0.55)

    def shield_detail(p, m):
        q = p - sc
        y, z = q[..., 1], q[..., 2]
        halfw = np.where(z > 0, 2.6, 2.6 * np.clip(1 + z / 4.6, 0, 1))
        rim = (z > 2.6) | ((np.abs(y) > halfw - 0.45) & (z > -1.0))
        crs = ((np.abs(y) < 0.5) & (z > -3.0) & (z < 2.4)) | ((np.abs(z - 0.6) < 0.5) & (np.abs(y) < 1.8))
        return np.where(rim, 2, np.where(crs, 3, m))
    # shield is turned ~35 degrees towards the front
    rot = np.radians(-35)
    cr, sr = np.cos(rot), np.sin(rot)

    def shield_rot(p):
        q = p - sc
        x = q[..., 0] * cr - q[..., 1] * sr
        y = q[..., 0] * sr + q[..., 1] * cr
        r = np.stack([x, y, q[..., 2]], -1) + sc
        return shield(r)

    def shield_rot_detail(p, m):
        q = p - sc
        x = q[..., 0] * cr - q[..., 1] * sr
        y = q[..., 0] * sr + q[..., 1] * cr
        return shield_detail(np.stack([x, y, q[..., 2]], -1) + sc, m)
    add(shield_rot, 6, shield_rot_detail)
    return parts


# ---------------------------------------------------------------- renderer
def render_frame(direction, pose):
    parts = build(pose)
    phi = np.radians(direction * 45.0)          # 0 = facing the viewer (S), counter-clockwise on screen
    f = np.array([np.sin(phi), np.cos(phi), 0.0])            # facing, world (u right, v toward viewer, z up)
    rgt = np.array([-f[1], f[0], 0.0])                        # character's right hand side
    cam_f = np.array([0.0, -np.cos(ELEV), -np.sin(ELEV)])    # view direction
    cam_u = np.array([0.0, -np.sin(ELEV), np.cos(ELEV)])     # screen up
    cam_r = np.array([1.0, 0.0, 0.0])
    n = SIZE * SS
    ys, xs = np.mgrid[0:n, 0:n]
    sx = (xs + 0.5) / SS - ANCHOR[0]
    sy = (ys + 0.5) / SS - ANCHOR[1]
    origin = sx[..., None] * cam_r - sy[..., None] * cam_u - 40 * cam_f

    def to_model(pw):
        return np.stack([pw @ rgt, pw @ f, pw[..., 2]], -1)

    def scene(pw):
        pm = to_model(pw)
        d = np.full(pw.shape[:-1], 1e9)
        mat = np.zeros(pw.shape[:-1], dtype=np.int32)
        for k, (fn, m, det) in enumerate(parts):
            dk = fn(pm)
            closer = dk < d
            d = np.where(closer, dk, d)
            mat = np.where(closer, k, mat)
        return d, mat, pm

    t = np.zeros((n, n))
    hit = np.zeros((n, n), dtype=bool)
    for _ in range(90):
        p = origin + t[..., None] * cam_f
        d, _, _ = scene(p)
        hit |= d < 0.02
        t = np.where(hit, t, t + np.maximum(d, 0.02) * 0.9)
        if (t > 80).all():
            break
    p = origin + t[..., None] * cam_f
    d, part, pm = scene(p)
    hit &= t < 80
    # material per pixel (+ painted details)
    mat = np.zeros((n, n), dtype=np.int32)
    for k, (fn, m, det) in enumerate(parts):
        sel = part == k
        if det is None:
            mat[sel] = m
        else:
            mat[sel] = det(pm, np.full((n, n), m))[sel]
    # normals by central differences
    e = 0.05
    nrm = np.zeros((n, n, 3))
    for i in range(3):
        o = np.zeros(3); o[i] = e
        nrm[..., i] = scene(p + o)[0] - scene(p - o)[0]
    nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True) + 1e-9
    light = normalize([-0.55, 0.55, 0.9])
    lam = np.clip(nrm @ light, 0, 1)
    rim = np.clip(1 + nrm @ cam_f, 0, 1) ** 3
    spec = np.clip(nrm @ normalize(light - cam_f), 0, 1) ** 24
    shade = 0.28 + 0.62 * lam + 0.25 * spec - 0.12 * rim
    mat[~hit] = 0

    # reduce to 32x32: majority material per block, averaged shade
    out = np.zeros((SIZE, SIZE), dtype=np.uint8)
    mb = mat.reshape(SIZE, SS, SIZE, SS).transpose(0, 2, 1, 3).reshape(SIZE, SIZE, SS * SS)
    sb = shade.reshape(SIZE, SS, SIZE, SS).transpose(0, 2, 1, 3).reshape(SIZE, SIZE, SS * SS)
    for y in range(SIZE):
        for x in range(SIZE):
            ms = mb[y, x]
            vals, cnt = np.unique(ms, return_counts=True)
            bg = cnt[vals == 0].sum()
            nz = vals != 0
            if not nz.any():
                continue
            thin = (vals[nz] == 8) & (cnt[nz] >= 4)       # keep the 1-pixel sword blade
            if bg > SS * SS * 0.6 and not thin.any():
                continue
            m = 8 if thin.any() else vals[nz][np.argmax(cnt[nz])]
            s = sb[y, x][ms == m].mean()
            ramp = RAMP[m]
            lvl = 0 if s < 0.42 else 1 if s < 0.66 else 2 if s < 0.9 else 3
            out[y, x] = ramp[lvl]
    # ground shadow then outline
    fig = out != 0
    yy, xx = np.mgrid[0:SIZE, 0:SIZE]
    sh = (((xx - 16) / 7.5) ** 2 + ((yy - 29.5) / 2.2) ** 2 <= 1) & ~fig
    res = out.copy()
    res[sh] = SHADOW
    pad = np.pad(fig, 1)
    near = pad[:-2, 1:-1] | pad[2:, 1:-1] | pad[1:-1, :-2] | pad[1:-1, 2:]
    res[near & ~fig] = OUT
    return res


if __name__ == "__main__":
    import sys
    from PIL import Image
    PAL = [(0, 0, 0), (18, 14, 16), (64, 72, 86), (128, 138, 152), (204, 210, 220), (138, 98, 30), (214, 172, 64),
           (230, 224, 204), (160, 150, 128), (176, 32, 30), (102, 16, 16), (222, 172, 132), (104, 66, 38),
           (14, 12, 14), (52, 84, 168), (16, 16, 30)]
    sheet = np.zeros((SIZE * len(POSES), SIZE * 8), dtype=np.uint8)
    for pi, pose in enumerate(POSES):
        for d in range(8):
            sheet[pi * SIZE:(pi + 1) * SIZE, d * SIZE:(d + 1) * SIZE] = render_frame(d, pose)
    # indexed PNG: pixel value = palette slot (read back by gen_assets.py); colours only for viewing
    img = Image.fromarray(sheet, "P")
    img.putpalette([c for rgb in PAL for c in rgb])
    img.save(sys.argv[1] if len(sys.argv) > 1 else "paladin_sheet.png")
    print("rendered", len(POSES) * 8, "frames")
