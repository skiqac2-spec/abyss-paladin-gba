// Korean dialogue box: translucent panel (BG2, alpha blended over the scene) + a 26x6 tile text
// canvas on BG0 into which 2bpp Galmuri glyphs are drawn one by one (typewriter effect).
// Only the glyphs the script uses are stored in ROM (see tools/gen_story.py).
#include "game.h"
#include "story.h"

#define TXT SCREENBLOCK(31)
#define PMAP SCREENBLOCK(29)
#define CV_BASE 192           // canvas tiles in charblock 1 (after font + HUD tiles)
#define CV_W 26
#define CV_H 6
#define CV_X 2
#define CV_Y 12
#define PN_BASE 352           // panel tiles (fill, edges, corners)
#define BOX_X0 1
#define BOX_X1 28
#define BOX_Y0 11
#define BOX_Y1 17
#define BP_DLG 11             // white text with dark edge
#define BP_DLGN 12            // speaker name (gold)
#define BP_PANEL 13
#define BP_NARR 14            // narration (pale lavender)

EWRAM_BSS static u32 canvas[CV_W * CV_H * 8];
static int dirty;
static int pending = -1;
static u8 healTurn;

static u16 rgb(int r, int g, int b) { return (u16)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)); }

void dialog_init_gfx(void) {
    // palettes: index 1 = letter, 2 = edge
    PAL_BG[BP_DLG * 16 + 1] = rgb(248, 244, 232);  PAL_BG[BP_DLG * 16 + 2] = rgb(8, 8, 20);
    PAL_BG[BP_DLGN * 16 + 1] = rgb(240, 196, 92);  PAL_BG[BP_DLGN * 16 + 2] = rgb(24, 12, 4);
    PAL_BG[BP_NARR * 16 + 1] = rgb(206, 208, 236); PAL_BG[BP_NARR * 16 + 2] = rgb(6, 6, 18);
    PAL_BG[BP_PANEL * 16 + 1] = rgb(14, 18, 44);   // fill (blended with the scene)
    PAL_BG[BP_PANEL * 16 + 2] = rgb(214, 176, 92); // bright edge
    PAL_BG[BP_PANEL * 16 + 3] = rgb(90, 64, 30);   // dark edge
    // panel tiles: 0 fill, 1 top, 2 left, 3 right, 4 top-left, 5 top-right
    volatile u32* t = CHARBLOCK(1) + PN_BASE * 8;
    for (int k = 0; k < 6; k++)
        for (int y = 0; y < 8; y++) {
            u32 row = 0;
            for (int x = 0; x < 8; x++) {
                int c = 1;
                int top = (k == 1 || k == 4 || k == 5), left = (k == 2 || k == 4), right = (k == 3 || k == 5);
                if (top && y == 2) c = 2;
                if (top && y == 3) c = 3;
                if (top && y < 2) c = 0;
                if (left && x == 2 && y >= (top ? 2 : 0)) c = 2;
                if (left && x == 3 && y >= (top ? 3 : 0)) c = c == 2 ? 2 : 3;
                if (left && x < 2) c = 0;
                if (right && x == 5 && y >= (top ? 2 : 0)) c = 2;
                if (right && x == 4 && y >= (top ? 3 : 0)) c = c == 2 ? 2 : 3;
                if (right && x > 5) c = 0;
                row |= (u32)c << (x * 4);
            }
            t[k * 8 + y] = row;
        }
}

static void cv_clear(void) {
    for (int i = 0; i < CV_W * CV_H * 8; i++) canvas[i] = 0;
    dirty = 1;
}

static void cv_glyph(int x, int y, int g) {
    const u32* rows = glyph_rows[g];
    for (int r = 0; r < GLYPH_H; r++) {
        u32 bits = rows[r];
        int py = y + r;
        if (!bits || py < 0 || py >= CV_H * 8) continue;
        for (int c = 0; bits; c++, bits >>= 2) {
            u32 v = bits & 3;
            int px = x + c;
            if (!v || px < 0 || px >= CV_W * 8) continue;
            u32* w = &canvas[((py >> 3) * CV_W + (px >> 3)) * 8 + (py & 7)];
            int sh = (px & 7) * 4;
            u32 cur = (*w >> sh) & 15;
            if (v == 1 || cur == 0) *w = (*w & ~(15u << sh)) | (v << sh);
        }
    }
    dirty = 1;
}

static void cv_erase(int x0, int y0, int w, int h) {
    for (int py = y0; py < y0 + h; py++)
        for (int px = x0; px < x0 + w; px++) {
            u32* wd = &canvas[((py >> 3) * CV_W + (px >> 3)) * 8 + (py & 7)];
            *wd &= ~(15u << ((px & 7) * 4));
        }
    dirty = 1;
}

static void cv_flush(void) {
    if (!dirty) return;
    dma3_copy32(CHARBLOCK(1) + CV_BASE * 8, canvas, CV_W * CV_H * 8);
    dirty = 0;
}

static void cv_palette(int named) {
    for (int r = 0; r < CV_H; r++) {
        int pal = named ? (r < 2 ? BP_DLGN : BP_DLG) : BP_NARR;
        for (int c = 0; c < CV_W; c++) TXT[(CV_Y + r) * 32 + CV_X + c] = (u16)((CV_BASE + r * CV_W + c) | (pal << 12));
    }
}

static void box_show(void) {
    light_update(0);
    dma3_fill32(PMAP, 0, 512);
    for (int y = BOX_Y0; y <= BOX_Y1; y++)
        for (int x = BOX_X0; x <= BOX_X1; x++) {
            int k = 0;
            if (y == BOX_Y0) k = (x == BOX_X0) ? 4 : (x == BOX_X1) ? 5 : 1;
            else if (x == BOX_X0) k = 2;
            else if (x == BOX_X1) k = 3;
            PMAP[y * 32 + x] = (u16)((PN_BASE + k) | (BP_PANEL << 12));
            TXT[y * 32 + x] = 0;
        }
    cv_clear();
    cv_flush();
    cv_palette(1);
    REG_BGCNT(2) = 0 | (1 << 2) | (29 << 8);        // priority 0, charblock 1, screenblock 29
    REG_BLDCNT = 0x0004 | 0x0040 | 0x3200;          // BG2 alpha-blended over BG1 / OBJ / backdrop
    REG_BLDALPHA = 10 | (6 << 8);
    REG_DISPCNT |= 0x0400;
}

static void box_hide(void) {
    REG_DISPCNT &= ~0x0400;
    for (int y = BOX_Y0; y <= BOX_Y1; y++)
        for (int x = 0; x < 32; x++) TXT[y * 32 + x] = 0;
    REG_BLDCNT = 0;
    REG_BGCNT(2) = 3 | (2 << 2) | (28 << 8);        // back to the title screen layer
}

static int frame_step(void) {          // returns key presses of this frame
    read_keys();
    vsync();
    cv_flush();
    snd_update();
    return keysNew;
}

int dialog_has(int ev) {
    return ev >= 0 && ev < EV_COUNT && story_events[ACT - 1][ev] != 0xFFFF;
}

void dialog_run(int ev) {
    if (!dialog_has(ev)) return;
    const u16* p = &story_stream[story_events[ACT - 1][ev]];
    box_show();
    int skip = 0;
    while (*p != ST_END && !skip) {
        cv_clear();
        int spk = *p++ & 0x0FFF;
        cv_palette(spk != 0);
        if (spk) {
            int x = 2;
            for (const u16* q = &speaker_stream[speaker_off[spk]]; *q != ST_END; q++) { cv_glyph(x, 1, *q); x += glyph_adv[*q]; }
        }
        int y0 = spk ? 17 : 9, line = 0, x = 2, fast = 0;
        while (*p != ST_PAGE) {
            u16 g = *p++;
            if (g == ST_NL) { line++; x = 2; continue; }
            cv_glyph(x, y0 + line * 15, g);
            x += glyph_adv[g];
            if (!fast && g != GLYPH_SPACE) {
                int k = frame_step();
                if (k & (KEY_A | KEY_B)) fast = 1;
                if (k & KEY_START) { skip = 1; fast = 1; }
            }
        }
        p++;
        if (skip) break;
        // wait for A with a blinking "next" marker
        for (int t = 0;; t++) {
            if ((t & 31) == 0) cv_glyph(CV_W * 8 - 12, CV_H * 8 - 15, GLYPH_NEXT);
            if ((t & 31) == 18) cv_erase(CV_W * 8 - 12, CV_H * 8 - 15, 12, 14);
            int k = frame_step();
            if (k & (KEY_A | KEY_B)) break;
            if (k & KEY_START) { skip = 1; break; }
        }
        sfx_menu();
    }
    box_hide();
}

void dialog_queue(int ev) { if (dialog_has(ev)) pending = ev; }

void dialog_pending(void) {
    if (pending < 0) return;
    int ev = pending;
    pending = -1;
    dialog_run(ev);
}

void dialog_healer(void) {
    int n = story_heal_count[ACT - 1];
    if (!n) return;
    dialog_run(EV_HEAL0 + healTurn % n);
    healTurn++;
}
