// Tiny sound effects using the GBA's built-in PSG channels
#include "game.h"

static u16 arp[6];
static int arpN, arpPos, arpT;

void snd_init(void) {
    REG_SNDSTAT = 0x80;
    REG_SNDDMGCNT = 0xFF77;
    REG_SNDDSCNT = 2;
    arpN = 0;
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
