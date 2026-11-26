// Sound: PSG sound effects + streamed BGM on DirectSound A
// BGM runs at 13379 Hz = exactly 224 samples per frame.  A VBlank interrupt restarts DMA1 on the
// buffer that was filled during the previous frame and decodes the next 224 samples
// (4-bit IMA ADPCM for the town theme, plain 8-bit PCM for dungeon/boss music).
#include "game.h"
#include "music.h"

#define MBUF 224
static s8 mbuf[2][MBUF] __attribute__((aligned(4)));
static u8 mbank;
static const MusicTrack* mtr;
static u32 mpos;
static s32 mpred;
static s32 midx;
static s32 mvol;                 // 0..256 current volume (fades)
static volatile u8 mCur = MUS_NONE, mWant = MUS_NONE;

static const u16 adStep[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
    60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
    544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const s8 adIdx[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static IWRAM_CODE void music_start_track(int id) {
    mCur = id;
    if (id == MUS_NONE) { mtr = 0; return; }
    mtr = &music_tracks[id];
    mpos = 0;
    mpred = (s16)(mtr->start & 0xFFFF);
    midx = mtr->start >> 16;
}

static IWRAM_CODE void music_fill(s8* out) {
    // fade handling: fade out the old track, then start the new one
    if (mWant != mCur) {
        mvol -= 24;
        if (mvol <= 0) { mvol = 0; music_start_track(mWant); }
    } else if (mvol < 256) {
        mvol += 16; if (mvol > 256) mvol = 256;
    }
    const MusicTrack* t = mtr;
    if (!t || mvol == 0) {
        u32* o = (u32*)out;
        for (int i = 0; i < MBUF / 4; i++) o[i] = 0;
        return;
    }
    u32 pos = mpos, len = t->len;
    s32 v = mvol;
    if (t->adpcm) {
        s32 pred = mpred, idx = midx;
        const u8* d = t->data;
        for (int i = 0; i < MBUF; i++) {
            u32 code = (d[pos >> 1] >> ((pos & 1) << 2)) & 15;
            s32 step = adStep[idx];
            s32 dq = step >> 3;
            if (code & 4) dq += step;
            if (code & 2) dq += step >> 1;
            if (code & 1) dq += step >> 2;
            if (code & 8) { pred -= dq; if (pred < -32768) pred = -32768; }
            else { pred += dq; if (pred > 32767) pred = 32767; }
            idx += adIdx[code & 7];
            if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
            out[i] = (s8)((pred * v) >> 16);
            if (++pos >= len) pos = 0;     // loop (decoder state carries over, see gen_music.py)
        }
        mpred = pred; midx = idx;
    } else {
        const s8* d = (const s8*)t->data;
        for (int i = 0; i < MBUF; i++) {
            out[i] = (s8)((d[pos] * v) >> 8);
            if (++pos >= len) pos = 0;
        }
    }
    mpos = pos;
}

IWRAM_CODE void irq_main(void) {
    u16 f = REG_IF;
    if (f & 1) {
        REG_DMA1CNT_H = 0;
        REG_DMA1SAD = (u32)mbuf[mbank];
        REG_DMA1CNT_H = 0xB640;      // enable, FIFO timing, 32-bit, repeat, fixed destination
        mbank ^= 1;
        music_fill(mbuf[mbank]);
    }
    REG_IF = f;
    BIOS_IF |= f;
}

void music_play(int id) { mWant = (u8)id; }
int music_current(void) { return mWant; }

static void music_init(void) {
    for (int i = 0; i < MBUF; i++) { mbuf[0][i] = 0; mbuf[1][i] = 0; }
    REG_TM0CNT_H = 0;
    REG_TM0CNT_L = 65536 - 1254;     // 16.78 MHz / 1254 = 13379 Hz
    REG_TM0CNT_H = 0x0080;
    REG_DMA1DAD = REG_FIFO_A;
    REG_DMA1SAD = (u32)mbuf[0];
    REG_DMA1CNT_H = 0xB640;
    mbank = 1;
    IRQ_VECTOR = (u32)irq_main;
    REG_DISPSTAT |= 0x0008;          // VBlank IRQ
    REG_IE = 0x0001;
    REG_IME = 1;
}

static u16 arp[6];
static int arpN, arpPos, arpT;

void snd_init(void) {
    REG_SNDSTAT = 0x80;
    REG_SNDDMGCNT = 0xFF77;
    REG_SNDDSCNT = 0x0B06;           // PSG 100%, DirectSound A 100% on both speakers, timer 0, FIFO reset
    arpN = 0;
    music_init();
}

static void sq1(u16 sweep, u16 env, u16 freq) {
    REG_SND1SWEEP = sweep; REG_SND1CNT = env; REG_SND1FREQ = 0x8000 | freq;
}
static void sq2(u16 env, u16 freq) { REG_SND2CNT = env; REG_SND2FREQ = 0x8000 | freq; }
static void noise(u16 env, u16 cfg) { REG_SND4CNT = env; REG_SND4FREQ = 0x8000 | cfg; }

void snd_update(void) {
    if (arpPos < arpN && --arpT <= 0) {
        sq2(0xA180, arp[arpPos++]);
        arpT = 5;
    }
}

static void play_arp(const u16* f, int n) {
    for (int i = 0; i < n; i++) arp[i] = f[i];
    arpN = n; arpPos = 0; arpT = 1;
}

void sfx_swing(void) { noise(0x3100, 0x0021); }
void sfx_hit(void) { noise(0xA200, 0x0052); sq1(0x0079, 0x8280, 1200); }
void sfx_bolt(void) { sq1(0x0016, 0xB2C0, 1600); }
void sfx_potion(void) { sq1(0x0026, 0x9380, 1400); }
void sfx_pickup(void) { sq2(0x7180, 1850); }
void sfx_menu(void) { sq2(0x5140, 1750); }
void sfx_die(void) { noise(0xF700, 0x0073); sq1(0x007F, 0xF780, 1500); }
void sfx_portal(void) { sq1(0x0035, 0xA4C0, 1300); }
void sfx_levelup(void) { static const u16 f[5] = { 1546, 1650, 1714, 1798, 1849 }; play_arp(f, 5); }
void sfx_boss(void) { noise(0xF700, 0x0083); static const u16 f[4] = { 1714, 1650, 1546, 1798 }; play_arp(f, 4); }
