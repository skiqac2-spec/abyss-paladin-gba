#!/usr/bin/env python3
"""Original BGM for ABYSS PALADIN, synthesized from note lists (no samples, no external audio).

Outputs (in the build folder):
  mus_town.bin     4-bit IMA ADPCM  (town / title theme)
  mus_dungeon.bin  8-bit signed PCM (dungeon theme)
  mus_boss.bin     8-bit signed PCM (boss battle)
  music.s          .incbin wrapper + track table
  music.h          track ids
Everything is mono at 13379 Hz (= exactly 224 samples per GBA frame).
Only numpy is required.  usage: gen_music.py build_dir [--wav]
"""
import os
import sys
import wave
import numpy as np

SR = 13379
OUT = sys.argv[1] if len(sys.argv) > 1 else "build"
WAV = "--wav" in sys.argv
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(1234)


def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)


def env_adsr(n, a, d, s, r, total):
    """a,d,r in seconds; s level; total = note length in samples (release starts after it)"""
    na, nd, nr = max(1, int(a * SR)), max(1, int(d * SR)), max(1, int(r * SR))
    e = np.zeros(n)
    t = np.arange(n)
    e = np.where(t < na, t / na, 0.0)
    m = (t >= na) & (t < na + nd)
    e[m] = 1 - (1 - s) * (t[m] - na) / nd
    e[(t >= na + nd)] = s
    rel = t >= total
    e[rel] = e[min(total, n - 1)] * np.clip(1 - (t[rel] - total) / nr, 0, 1)
    return e


def harmonics(f, n, amps, decays=None, phase_rand=True):
    t = np.arange(n) / SR
    out = np.zeros(n)
    for k, a in enumerate(amps, start=1):
        fk = f * k
        if fk > SR * 0.45 or a == 0:
            continue
        ph = rng.uniform(0, 2 * np.pi) if phase_rand else 0
        s = a * np.sin(2 * np.pi * fk * t + ph)
        if decays is not None:
            s *= np.exp(-t * decays[k - 1])
        out += s
    return out


# ---------------------------------------------------------------- instruments
def pluck(f, dur, vel):
    n = int((dur + 1.2) * SR)
    amps = [1 / k ** 1.1 for k in range(1, 11)]
    dec = [2.2 + 1.6 * k for k in range(1, 11)]
    s = harmonics(f, n, amps, dec)
    s *= np.clip(np.arange(n) / (0.004 * SR), 0, 1)
    s *= env_adsr(n, 0.002, 0.05, 1.0, 0.25, int(dur * SR) + int(0.6 * SR))
    return s * vel * 0.5


def pad(f, dur, vel):
    n = int((dur + 0.9) * SR)
    s = np.zeros(n)
    for det in (-0.06, 0.0, 0.07):
        s += harmonics(f * 2 ** (det / 12), n, [1, 0.45, 0.25, 0.12, 0.06])
    s *= env_adsr(n, 0.45, 0.3, 0.8, 0.8, int(dur * SR))
    return s * vel * 0.12


def bass(f, dur, vel):
    n = int((dur + 0.4) * SR)
    s = harmonics(f, n, [1, 0.5, 0.2, 0.08], [1.2, 2.5, 4, 6], phase_rand=False)
    s *= env_adsr(n, 0.006, 0.2, 0.7, 0.12, int(dur * SR))
    return s * vel * 0.55


def flute(f, dur, vel):
    n = int((dur + 0.4) * SR)
    t = np.arange(n) / SR
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.2 * t) * np.clip((t - 0.25) / 0.3, 0, 1)
    ph = 2 * np.pi * np.cumsum(f * vib) / SR
    s = np.sin(ph) + 0.22 * np.sin(2 * ph) + 0.08 * np.sin(3 * ph)
    s += 0.02 * rng.standard_normal(n) * env_adsr(n, 0.02, 0.1, 0.2, 0.1, int(dur * SR))
    s *= env_adsr(n, 0.07, 0.15, 0.85, 0.25, int(dur * SR))
    return s * vel * 0.32


def bell(f, dur, vel):
    n = int((dur + 2.0) * SR)
    t = np.arange(n) / SR
    s = np.zeros(n)
    for ratio, a, dcy in ((1, 1, 1.4), (2.76, 0.5, 2.6), (5.40, 0.25, 4.5), (8.93, 0.12, 7)):
        if f * ratio < SR * 0.45:
            s += a * np.sin(2 * np.pi * f * ratio * t) * np.exp(-t * dcy)
    s *= np.clip(np.arange(n) / 20, 0, 1)
    return s * vel * 0.3


def brass(f, dur, vel):
    n = int((dur + 0.3) * SR)
    t = np.arange(n) / SR
    out = np.zeros(n)
    bright = np.clip(t / 0.08, 0, 1) * (0.6 + 0.4 * np.exp(-t * 3))
    for k in range(1, 12):
        if f * k > SR * 0.45:
            break
        out += (1 / k) * np.sin(2 * np.pi * f * k * t) * bright ** (k * 0.35)
    out *= env_adsr(n, 0.03, 0.2, 0.75, 0.15, int(dur * SR))
    return out * vel * 0.28


def choir(f, dur, vel):
    n = int((dur + 1.0) * SR)
    t = np.arange(n) / SR
    s = np.zeros(n)
    for det, ph in ((-0.1, 0), (0.0, 1), (0.11, 2)):
        vib = 1 + 0.003 * np.sin(2 * np.pi * (4.6 + ph * 0.3) * t + ph)
        p = 2 * np.pi * np.cumsum(f * 2 ** (det / 12) * vib) / SR
        s += np.sin(p) + 0.3 * np.sin(2 * p) + 0.18 * np.sin(3 * p) + 0.1 * np.sin(5 * p)
    s *= env_adsr(n, 0.6, 0.4, 0.85, 0.9, int(dur * SR))
    return s * vel * 0.09


def kick(vel):
    n = int(0.35 * SR)
    t = np.arange(n) / SR
    f = 45 + 80 * np.exp(-t * 30)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 9)
    return s * vel * 0.9


def snare(vel):
    n = int(0.25 * SR)
    t = np.arange(n) / SR
    nz = rng.standard_normal(n)
    nz = np.diff(np.concatenate([[0], nz])) * 0.6 + nz * 0.4
    s = nz * np.exp(-t * 18) * 0.55 + np.sin(2 * np.pi * 185 * t) * np.exp(-t * 25) * 0.6
    return s * vel * 0.6


def hat(vel):
    n = int(0.06 * SR)
    t = np.arange(n) / SR
    nz = np.diff(np.concatenate([[0], rng.standard_normal(n)]))
    return nz * np.exp(-t * 70) * vel * 0.18


def tom(vel, f0=150):
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    f = f0 * (0.6 + 0.4 * np.exp(-t * 12))
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 7)
    s += rng.standard_normal(n) * np.exp(-t * 40) * 0.15
    return s * vel * 0.7


INSTR = dict(pluck=pluck, pad=pad, bass=bass, flute=flute, bell=bell, brass=brass, choir=choir)
DRUM = dict(kick=kick, snare=snare, hat=hat, tom=tom, ltom=lambda v: tom(v, 95))


# ---------------------------------------------------------------- song rendering
class Song:
    def __init__(self, bpm, beats):
        self.bpm = bpm
        self.beats = beats
        self.spb = 60.0 / bpm
        self.events = []        # (beat, kind, args)

    def note(self, beat, dur, midi, inst, vel=1.0):
        self.events.append((beat, inst, (midi, dur, vel)))

    def drum(self, beat, kind, vel=1.0):
        self.events.append((beat, kind, (vel,)))

    def chord(self, beat, dur, notes, inst, vel=1.0):
        for m in notes:
            self.note(beat, dur, m, inst, vel)

    def render(self):
        L = int(round(self.beats * self.spb * SR))
        buf = np.zeros(L * 3)
        for rep in range(2):                     # render twice: the second pass carries the tails
            for beat, kind, a in self.events:
                st = int(round((beat * self.spb) * SR)) + rep * L
                if kind in DRUM:
                    s = DRUM[kind](a[0])
                else:
                    midi, dur, vel = a
                    s = INSTR[kind](mtof(midi), dur * self.spb, vel)
                buf[st:st + len(s)] += s[:len(buf) - st]
        return buf, L


def reverb(x, mix=0.25):
    out = np.zeros_like(x)
    for d, g in ((1116, 0.78), (1188, 0.76), (1277, 0.74), (1356, 0.72)):
        d = d * SR // 44100 * 2
        y = x.copy()
        for k in range(d, len(y), d):        # comb filter, block-vectorized
            e = min(k + d, len(y))
            y[k:e] += g * y[k - d:e - d]
        out += y
    out /= 4
    for d, g in ((225, 0.5), (556, 0.5)):    # allpass diffusion
        d = d * SR // 44100 * 2
        y = np.zeros_like(out)
        xin = out
        for k in range(0, len(y), d):
            e = min(k + d, len(y))
            prev = y[k - d:e - d] if k >= d else np.zeros(e - k)
            prevx = xin[k - d:e - d] if k >= d else np.zeros(e - k)
            y[k:e] = -g * xin[k:e] + prevx + g * prev
        out = y
    return x * (1 - mix) + out * mix


def finish(song, rv=0.25, peak=100.0):
    buf, L = song.render()
    buf = reverb(buf, rv)
    loop = buf[L:2 * L]
    loop = loop / (np.max(np.abs(loop)) + 1e-9)
    # soft clip for density, then scale to 8-bit
    loop = np.tanh(loop * 1.6) / np.tanh(1.6)
    return loop * peak


# ---------------------------------------------------------------- compositions (original)
def town():
    # "Embers of the Camp" - D dorian, 78 bpm, 16 bars of 4/4
    s = Song(78, 64)
    D, E, F, G, A, Bb, B, C = 62, 64, 65, 67, 69, 70, 71, 72
    prog = [  # (root for bass, chord tones)
        (38, [50, 57, 62, 65]), (34, [46, 53, 58, 62]), (41, [53, 57, 60, 65]), (36, [48, 55, 60, 64]),
        (38, [50, 57, 62, 65]), (43, [55, 58, 62, 67]), (45, [57, 61, 64, 69]), (38, [50, 57, 62, 65]),
        (34, [46, 53, 58, 62]), (36, [48, 55, 60, 64]), (41, [53, 57, 60, 65]), (38, [50, 57, 62, 69]),
        (43, [55, 58, 62, 67]), (34, [46, 53, 58, 65]), (45, [57, 61, 64, 67]), (38, [50, 57, 62, 65]),
    ]
    arp = [0, 1, 2, 3, 2, 1, 2, 1]
    for bar, (root, ch) in enumerate(prog):
        b0 = bar * 4
        s.chord(b0, 4, [n + 12 for n in ch[1:]], "pad", 0.8)
        s.note(b0, 1.8, root, "bass", 0.9)
        s.note(b0 + 2.5, 1.2, root + 7, "bass", 0.6)
        for i, k in enumerate(arp):
            s.note(b0 + i * 0.5, 0.6, ch[k] + 12, "pluck", 0.55 if i % 2 else 0.75)
    mel = [  # (beat, dur, midi) - flute melody enters on bar 3
        (8, 1.5, A), (9.5, 0.5, G), (10, 1, F), (11, 1, E), (12, 3, D), (15, 1, E),
        (16, 1.5, F), (17.5, 0.5, G), (18, 1, A), (19, 1, C), (20, 2, Bb), (22, 1, A), (23, 1, G),
        (24, 3, A), (27, 1, E), (28, 4, D),
        (32, 1.5, D + 12), (33.5, 0.5, C), (34, 2, A), (36, 1.5, C), (37.5, 0.5, Bb), (38, 2, G),
        (40, 1, A), (41, 1, Bb), (42, 1, C), (43, 1, D + 12), (44, 3, A), (47, 1, G),
        (48, 1.5, F), (49.5, 0.5, G), (50, 2, A), (52, 1.5, Bb), (53.5, 0.5, A), (54, 2, G),
        (56, 2, E), (58, 2, F + 0), (60, 4, D),
    ]
    for b, d, m in mel:
        s.note(b, d, m + 12, "flute", 0.8)
    for b in (0, 16, 32, 48):
        s.note(b, 2, 86, "bell", 0.35)
    return finish(s, rv=0.32, peak=92)


def dungeon():
    # "Below the Barrow" - E phrygian, 84 bpm, 12 bars
    s = Song(84, 48)
    roots = [40, 41, 40, 38, 40, 41, 43, 41, 40, 38, 36, 35]
    for bar, r in enumerate(roots):
        b0 = bar * 4
        s.chord(b0, 4, [r + 12, r + 19, r + 24], "choir", 0.9)
        s.note(b0, 0.5, r, "bass", 1.0)
        s.note(b0 + 0.75, 0.5, r, "bass", 0.7)
        s.note(b0 + 2, 0.5, r, "bass", 0.9)
        s.note(b0 + 3.5, 0.5, r + 1 if bar % 2 == 0 else r - 2, "bass", 0.6)
        s.drum(b0, "ltom", 0.9)
        s.drum(b0 + 2, "tom", 0.55)
        s.drum(b0 + 2.75, "ltom", 0.45)
        for h in range(8):
            s.drum(b0 + h * 0.5, "hat", 0.35 if h % 2 else 0.5)
    motif = [(0, 1, 76), (1, 0.5, 77), (1.5, 1.5, 76), (3, 1, 74)]
    for rep, b in enumerate((8, 24, 40)):
        for db, d, m in motif:
            s.note(b + db, d, m - (rep == 2) * 2, "bell", 0.6)
    for b, d, m in ((16, 2, 64), (18, 1, 65), (19, 1, 67), (20, 4, 64), (32, 2, 71), (34, 2, 72), (36, 4, 71), (44, 4, 64)):
        s.note(b, d, m, "flute", 0.45)
    return finish(s, rv=0.38, peak=96)


def boss():
    # "Throne of the Guardian" - C minor, 144 bpm, 16 bars
    s = Song(144, 64)
    prog = [36, 36, 32, 32, 34, 34, 31, 31, 36, 36, 32, 32, 34, 34, 31, 35]
    chords = {36: [60, 63, 67], 32: [56, 60, 63], 34: [58, 62, 65], 31: [55, 59, 62], 35: [55, 59, 62, 65]}
    for bar, r in enumerate(prog):
        b0 = bar * 4
        for e in range(8):
            s.note(b0 + e * 0.5, 0.4, r + (12 if e in (3, 6) else 0), "bass", 0.85 if e % 2 == 0 else 0.6)
        s.drum(b0, "kick", 1.0); s.drum(b0 + 1.5, "kick", 0.7); s.drum(b0 + 2, "kick", 0.9)
        s.drum(b0 + 1, "snare", 0.9); s.drum(b0 + 3, "snare", 0.95)
        if bar % 4 == 3:
            s.drum(b0 + 3.5, "tom", 0.8); s.drum(b0 + 3.75, "ltom", 0.8)
        for h in range(8):
            s.drum(b0 + h * 0.5, "hat", 0.4 if h % 2 else 0.25)
        if bar % 2 == 0:
            s.chord(b0, 0.5, chords[r], "brass", 0.75)
            s.chord(b0 + 1.5, 0.5, chords[r], "brass", 0.6)
            s.chord(b0 + 3, 1, chords[r], "brass", 0.7)
        s.chord(b0, 4, [n + 12 for n in chords[r][:2]], "choir", 0.7)
    lead = [(32, 2, 72), (34, 1, 74), (35, 1, 75), (36, 3, 74), (39, 1, 72), (40, 2, 68), (42, 2, 70),
            (44, 4, 67), (48, 2, 72), (50, 1, 75), (51, 1, 77), (52, 3, 79), (55, 1, 77), (56, 2, 75),
            (58, 2, 74), (60, 4, 71)]
    for b, d, m in lead:
        s.note(b, d, m, "brass", 0.55)
    return finish(s, rv=0.2, peak=104)


# ---------------------------------------------------------------- IMA ADPCM
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
        107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
        4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
        22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm_encode(x16):
    """Encodes the loop twice and keeps the second pass, so the decoder state at the loop
    point matches (no click when the loop wraps).  Returns (bytes, start_pred, start_idx)."""
    pred, idx = 0, 0
    nib = []
    start = None
    for i, v in enumerate(np.concatenate([x16, x16])):
        if i == len(x16):
            start = (pred, idx)
            nib = []
        step = STEP[idx]
        diff = int(v) - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        dq = step >> 3
        if diff >= step: code |= 4; diff -= step; dq += step
        step >>= 1
        if diff >= step: code |= 2; diff -= step; dq += step
        step >>= 1
        if diff >= step: code |= 1; dq += step
        pred = pred - dq if code & 8 else pred + dq
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code & 7]))
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    a = np.array(nib, dtype=np.uint8)
    return (a[0::2] | (a[1::2] << 4)).astype(np.uint8), start[0], start[1]


def adpcm_decode(data, n, pred, idx):      # reference decoder, mirrors the GBA code (for the preview wav)
    out = np.zeros(n)
    for i in range(n):
        code = (data[i >> 1] >> (4 * (i & 1))) & 15
        step = STEP[idx]
        dq = step >> 3
        if code & 4: dq += step
        if code & 2: dq += step >> 1
        if code & 1: dq += step >> 2
        pred = pred - dq if code & 8 else pred + dq
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code & 7]))
        out[i] = pred
    return out


def write_wav(name, x8):
    with wave.open(os.path.join(OUT, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(SR)
        w.writeframes((np.clip(x8, -128, 127).astype(np.int16) + 128).astype(np.uint8).tobytes())


tracks = []
for name, fn, kind in (("town", town, "adpcm"), ("dungeon", dungeon, "pcm"), ("boss", boss, "pcm")):
    x = fn()
    n = len(x)
    if kind == "adpcm":
        data, p0, i0 = adpcm_encode(np.round(x * 256).astype(np.int32))
        st = (p0 & 0xFFFF) | (i0 << 16)
        if WAV:
            write_wav("mus_%s.wav" % name, adpcm_decode(data, n, p0, i0) / 256)
    else:
        # light triangular dither before 8-bit quantization
        d = rng.uniform(-0.5, 0.5, n) + rng.uniform(-0.5, 0.5, n)
        data = np.clip(np.round(x + d), -128, 127).astype(np.int8).view(np.uint8)
        st = 0
        if WAV:
            write_wav("mus_%s.wav" % name, data.view(np.int8).astype(float))
    pad = (-len(data)) % 4
    data = np.concatenate([data, np.zeros(pad, dtype=np.uint8)])
    data.tofile(os.path.join(OUT, "mus_%s.bin" % name))
    tracks.append((name, n, kind == "adpcm", st))
    print("music %-8s %6.1fs %7d bytes (%s)" % (name, n / SR, len(data), kind))

with open(os.path.join(OUT, "music.s"), "w") as f:
    f.write("@ Generated by tools/gen_music.py - original music\n    .section .rodata\n    .align 2\n")
    for name, n, ad, st in tracks:
        f.write("mus_%s_data:\n    .incbin \"%s/mus_%s.bin\"\n    .align 2\n" % (name, OUT, name))
    f.write("    .global music_tracks\nmusic_tracks:\n")
    for name, n, ad, st in tracks:
        f.write("    .word mus_%s_data, %d, %d, %d\n" % (name, n, 1 if ad else 0, st))
with open(os.path.join(OUT, "music.h"), "w") as f:
    f.write("#ifndef MUSIC_H\n#define MUSIC_H\n")
    f.write("typedef struct { const unsigned char* data; unsigned int len; unsigned int adpcm; unsigned int start; } MusicTrack;\n")
    f.write("extern const MusicTrack music_tracks[%d];\n" % len(tracks))
    for i, (name, n, ad, st) in enumerate(tracks):
        f.write("#define MUS_%s %d\n" % (name.upper(), i))
    f.write("#define MUS_NONE 255\n#endif\n")
