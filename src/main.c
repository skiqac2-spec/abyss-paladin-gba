// ABYSS PALADIN - main loop, menus, HUD, save data and act passwords
#include "game.h"

Hero hero;
int curFloor;
u32 frameCount;
u16 keys, keysNew, keysOld;
static u32 rngState = 0x1234567;
static int msgTimer;
static int portalLock;
static int healerLock;

__attribute__((used)) const char saveTag[] = "SRAM_V113";   // tells emulators/flashcarts to use SRAM

const char* const actNames[5] = { "ACT I  ASHEN MOOR", "ACT II  SUNKEN DUNES", "ACT III  MIRE JUNGLE",
                                  "ACT IV  BURNING STEPS", "ACT V  FROZEN CROWN" };
const char* const floorNames[5][4] = {
    { "PILGRIM CAMP", "BARROW DEPTHS 1", "BARROW DEPTHS 2", "THORN SANCTUM" },
    { "OASIS POST", "KINGS TOMB 1", "KINGS TOMB 2", "SCARAB VAULT" },
    { "RIVER DOCK", "DROWNED TEMPLE 1", "DROWNED TEMPLE 2", "HOLLOW SHRINE" },
    { "LAST BASTION", "CINDER HALLS 1", "CINDER HALLS 2", "EMBER THRONE" },
    { "HEARTH HOLD", "GLACIER PATH 1", "GLACIER PATH 2", "PALE CITADEL" } };
static const char* const bossNames[5] = { "MATRON OF THORNS", "CARAPACE KING", "HOLLOW PROPHET", "LORD OF EMBERS", "THE PALE KING" };
static const char* const diffNames[3] = { "NORMAL", "NIGHTMARE", "HELL" };
static const char* const romanAct[5] = { "I", "II", "III", "IV", "V" };
static const char* const wNames[16] = { "SHORT SWORD", "BROAD SWORD", "LONG SWORD", "WAR SWORD", "BASTARD SWORD",
    "KNIGHT BLADE", "RUNE SWORD", "CRYSTAL SWORD", "TEMPLAR BLADE", "ZEALOT EDGE", "DIVINE SWORD", "SERAPH BLADE",
    "DAWN EDGE", "ARCHON SWORD", "RADIANT BLADE", "CELESTIAL EDGE" };
static const char* const aNames[16] = { "QUILTED ARMOR", "LEATHER ARMOR", "STUDDED LEATHER", "RING MAIL", "SCALE MAIL",
    "CHAIN MAIL", "BREAST PLATE", "SPLINT MAIL", "PLATE MAIL", "FIELD PLATE", "GOTHIC PLATE", "FULL PLATE",
    "TEMPLAR PLATE", "SACRED ARMOR", "ARCHON PLATE", "RADIANT AEGIS" };
static const char* const skillNames[4] = { "SMITE", "HOLY BOLT", "HOLY HAMMER", "MIGHT AURA" };

// ---------------------------------------------------------------- basics
u32 rnd(void) {
    u32 x = rngState;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rngState = x;
}
int rndr(int n) { return n > 0 ? (int)((rnd() >> 8) % (u32)n) : 0; }

void vsync(void) {
    while (REG_VCOUNT >= 160) {}
    while (REG_VCOUNT < 160) {}
    frameCount++;
}

void read_keys(void) {
    keysOld = keys;
    keys = ~REG_KEYINPUT & 0x3FF;
    keysNew = keys & ~keysOld;
    rngState ^= keys;
}

int itoa_buf(char* b, int v) {
    char t[12]; int n = 0, k = 0;
    if (v < 0) { b[k++] = '-'; v = -v; }
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) b[k++] = t[--n];
    b[k] = 0;
    return k;
}

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void scat(char* d, const char* s) { while (*d) d++; while ((*d++ = *s++)) {} }

// ---------------------------------------------------------------- text (BG0, SB31)
#define TXT SCREENBLOCK(31)
#define HUDT(n) (128 + (n))
#define BOXFONT 64

void text_clear(void) { dma3_fill32(TXT, 0, 512); }

void text_put(int x, int y, const char* s, int col) {
    while (*s && x < 30) {
        int c = *s++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = 32;
        int old = TXT[y * 32 + x] & 0x3FF;
        int boxed = (old >= BOXFONT && old < BOXFONT + 64) || (old >= HUDT(27) && old <= HUDT(37));
        TXT[y * 32 + x++] = (u16)((c - 32 + (boxed ? BOXFONT : 0)) | ((BP_FONT + col) << 12));
    }
}

void text_num(int x, int y, int v, int width, int col) {
    char b[12]; int n = itoa_buf(b, v);
    while (n < width) { text_put(x++, y, " ", col); width--; }
    text_put(x, y, b, col);
}

void text_center(int y, const char* s, int col) { text_put((30 - slen(s)) / 2, y, s, col); }

static void text_clear_row(int y) { for (int x = 0; x < 32; x++) TXT[y * 32 + x] = 0; }

static void box(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int t = 29;
            if (y == y0) t = 30; else if (y == y1) t = 31;
            if (x == x0) t = (y == y0) ? 34 : (y == y1) ? 36 : 32;
            else if (x == x1) t = (y == y0) ? 35 : (y == y1) ? 37 : 33;
            TXT[y * 32 + x] = (u16)(HUDT(t) | (BP_HUD << 12));
        }
}

void show_msg(const char* s, int col) {
    text_clear_row(2);
    text_center(2, s, col);
    msgTimer = 120;
}

const char* weapon_name(int t) {
    static char b[24];
    b[0] = 0; scat(b, wNames[(t >> 1) & 15]); if (t & 1) scat(b, "+");
    return b;
}
const char* armor_name(int t) {
    static char b[24];
    b[0] = 0; scat(b, aNames[(t >> 1) & 15]); if (t & 1) scat(b, "+");
    return b;
}

// ---------------------------------------------------------------- stats
int hero_maxhp(void) { return 70 + hero.level * 12 + hero.atier * 8; }
int hero_maxmp(void) { return 24 + hero.level * 4; }
u32 xp_need(int lv) { return 40 * lv + 8 * lv * lv; }
static int base_damage(void) { return (5 + hero.level * 2 + hero.wtier * 4) * (100 + hero.skill[SK_MIGHT] * 12) / 100; }
int player_damage_roll(void) {
    int b = base_damage();
    int d = b * (80 + rndr(41)) / 100;
    if (rndr(100) < 5) d *= 2;
    return d < 1 ? 1 : d;
}
static int skill_points(void) {
    int s = 0; for (int i = 0; i < SK_COUNT; i++) s += hero.skill[i];
    return hero.level - s;
}

// ---------------------------------------------------------------- HUD
static void bar(int x, int y, int tiles, int v, int max, int colorIdx) {
    if (max < 1) max = 1;
    int px = v * tiles * 8 / max;
    if (v > 0 && px == 0) px = 1;
    for (int i = 0; i < tiles; i++) {
        int f = px - i * 8; if (f < 0) f = 0; if (f > 8) f = 8;
        TXT[y * 32 + x + i] = (u16)(HUDT(colorIdx * 9 + f) | (BP_HUD << 12));
    }
}

void hud_draw(void) {
    bar(0, 18, 10, pl.hp, pl.maxhp, 0);
    bar(10, 18, 10, hero.xp, xp_need(hero.level), 1);
    bar(20, 18, 10, pl.mp, pl.maxmp, 2);
    for (int x = 0; x < 30; x++) TXT[19 * 32 + x] = (u16)(HUDT(28) | (BP_HUD << 12));
    text_put(0, 19, "*", C_RED);
    text_num(1, 19, pl.hp, 4, C_WHITE); text_put(5, 19, "/", C_GRAY); text_num(6, 19, pl.maxhp, 0, C_WHITE);
    text_put(11, 19, "L", C_GOLD); text_num(12, 19, hero.level, 0, C_GOLD);
    static const char* const sh[3] = { "SMITE", "BOLT", "HAMMR" };
    text_put(15, 19, sh[pl.cur], C_YELLOW);
    text_put(21, 19, "*", C_RED); text_num(22, 19, hero.hpPot, 0, C_WHITE);
    text_put(23, 19, "#", C_BLUE); text_num(24, 19, hero.mpPot, 0, C_WHITE);
    text_num(25, 19, pl.mp, 4, C_BLUE); text_put(29, 19, "#", C_BLUE);
    // top line: area and gold
    text_clear_row(0);
    text_put(0, 0, floorNames[ACT - 1][curFloor], curFloor ? C_GRAY : C_GREEN);
    char g[12]; g[0] = '$'; itoa_buf(g + 1, hero.gold);
    text_put(30 - slen(g), 0, g, C_GOLD);
    // boss bar
    text_clear_row(3); text_clear_row(4);
    if (bossAlive) {
        for (int i = 0; i < MAX_MON; i++)
            if (mons[i].active && mons[i].kind == 3 && mons[i].state != 0) {
                text_center(3, bossNames[ACT - 1], C_GOLD);
                bar(8, 4, 14, mons[i].hp, mons[i].maxhp, 0);
            }
    }
    if (msgTimer && --msgTimer == 0) text_clear_row(2);
}

// ---------------------------------------------------------------- save data (SRAM)
#define SAVE_SIZE 24
static void hero_to_bytes(u8* b) {
    b[0] = 'A'; b[1] = 'B'; b[2] = 'P'; b[3] = '1';
    b[4] = hero.diff; b[5] = hero.act; b[6] = hero.level; b[7] = hero.wtier; b[8] = hero.atier;
    for (int i = 0; i < 4; i++) b[9 + i] = hero.skill[i];
    b[13] = hero.hpPot; b[14] = hero.mpPot; b[15] = hero.deepest;
    for (int i = 0; i < 4; i++) { b[16 + i] = (hero.xp >> (8 * i)) & 0xFF; }
    b[20] = hero.gold & 0xFF; b[21] = (hero.gold >> 8) & 0xFF; b[22] = (hero.gold >> 16) & 0xFF;
    u8 c = 0x5A; for (int i = 0; i < 23; i++) c = (u8)(c * 31 + b[i]);
    b[23] = c;
}
static int bytes_to_hero(const u8* b) {
    if (b[0] != 'A' || b[1] != 'B' || b[2] != 'P' || b[3] != '1') return 0;
    u8 c = 0x5A; for (int i = 0; i < 23; i++) c = (u8)(c * 31 + b[i]);
    if (c != b[23]) return 0;
    hero.diff = b[4]; hero.act = b[5]; hero.level = b[6]; hero.wtier = b[7]; hero.atier = b[8];
    for (int i = 0; i < 4; i++) hero.skill[i] = b[9 + i];
    hero.hpPot = b[13]; hero.mpPot = b[14]; hero.deepest = b[15];
    hero.xp = b[16] | (b[17] << 8) | (b[18] << 16) | ((u32)b[19] << 24);
    hero.gold = b[20] | (b[21] << 8) | (b[22] << 16);
    if (hero.diff > 2 || hero.act < 1 || hero.act > 5 || hero.level < 1 || hero.level > 99) return 0;
    return 1;
}
void save_write(void) {
    u8 b[SAVE_SIZE];
    hero_to_bytes(b);
    for (int i = 0; i < SAVE_SIZE; i++) SRAM[i] = b[i];
    for (int i = 0; i < SAVE_SIZE; i++) SRAM[0x100 + i] = b[i];   // backup copy
}
static int save_read(void) {
    u8 b[SAVE_SIZE];
    for (int i = 0; i < SAVE_SIZE; i++) b[i] = SRAM[i];
    if (bytes_to_hero(b)) return 1;
    for (int i = 0; i < SAVE_SIZE; i++) b[i] = SRAM[0x100 + i];
    return bytes_to_hero(b);
}

// ---------------------------------------------------------------- act password (13 characters)
static const char B32[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static u64 pw_mask(u32 c) {
    u32 a = c * 2654435761u + 0x9E3779B9u, b2 = (c + 77) * 40503u ^ 0xA5A5F00Du;
    a ^= a >> 15; b2 ^= b2 << 7; b2 ^= b2 >> 11;
    return (((u64)a << 32) | b2) & ((1ull << 57) - 1);
}
static u8 pw_check(const u8* f) {
    u32 c = 0x5A;
    for (int i = 0; i < 13; i++) c = (c * 31 + f[i]) & 0xFFFF;
    return (u8)((c ^ (c >> 7)) & 127);
}
static const u8 pwBits[13] = { 2, 3, 7, 5, 5, 4, 4, 4, 4, 4, 4, 11, 0 };
static void hero_fields(u8* f, u16* gold) {
    f[0] = hero.diff; f[1] = hero.act - 1; f[2] = hero.level; f[3] = hero.wtier; f[4] = hero.atier;
    for (int i = 0; i < 4; i++) f[5 + i] = hero.skill[i] > 15 ? 15 : hero.skill[i];
    f[9] = hero.hpPot; f[10] = hero.mpPot;
    u32 g = hero.gold / 20; if (g > 2047) g = 2047;
    *gold = (u16)g; f[11] = g & 0xFF; f[12] = g >> 8;
}
static void make_password(char* out) {
    u8 f[13]; u16 g;
    hero_fields(f, &g);
    u64 v = 0;
    for (int i = 0; i < 11; i++) v = (v << pwBits[i]) | f[i];
    v = (v << 11) | g;
    u8 c = pw_check(f);
    v ^= pw_mask(c);
    v = (v << 7) | c;
    for (int k = 0; k < 13; k++) out[k] = B32[(v >> (60 - 5 * k)) & 31];
    out[13] = 0;
}
static int read_password(const char* in) {   // returns 1 if valid, fills hero
    u64 v = 0;
    for (int k = 0; k < 13; k++) {
        int idx = -1;
        for (int j = 0; j < 32; j++) if (B32[j] == in[k]) idx = j;
        if (idx < 0) return 0;
        if (k == 0 && idx > 15) return 0;
        v = (v << 5) | (u32)idx;
    }
    u8 c = v & 127;
    u64 d = (v >> 7) ^ pw_mask(c);
    u8 f[13];
    u16 g = d & 2047; d >>= 11;
    for (int i = 10; i >= 0; i--) { f[i] = d & ((1 << pwBits[i]) - 1); d >>= pwBits[i]; }
    f[11] = g & 0xFF; f[12] = g >> 8;
    if (pw_check(f) != c) return 0;
    if (f[0] > 2 || f[1] > 4 || f[2] < 1 || f[2] > 99) return 0;
    hero.diff = f[0]; hero.act = f[1] + 1; hero.level = f[2]; hero.wtier = f[3]; hero.atier = f[4];
    for (int i = 0; i < 4; i++) hero.skill[i] = f[5 + i];
    hero.hpPot = f[9]; hero.mpPot = f[10]; hero.gold = (u32)g * 20; hero.xp = 0; hero.deepest = 0;
    return 1;
}
static void show_password(int y) {
    char pw[14]; make_password(pw);
    char b[20]; int n = 0;
    for (int i = 0; i < 13; i++) { if (i == 4 || i == 8) b[n++] = '-'; b[n++] = pw[i]; }
    b[n] = 0;
    text_center(y, b, C_YELLOW);
}

// ---------------------------------------------------------------- graphics setup
static void load_common_gfx(void) {
    dma3_copy32(CHARBLOCK(1), font_tiles, sizeof(font_tiles) / 4);
    dma3_copy32(CHARBLOCK(1) + HUDT(0) * 8, hud_tiles, sizeof(hud_tiles) / 4);
    for (int c = 0; c < 8; c++)
        for (int i = 0; i < 16; i++) PAL_BG[(BP_FONT + c) * 16 + i] = pal_font[c][i];
    for (int i = 0; i < 16; i++) PAL_BG[BP_HUD * 16 + i] = pal_hud[i];
    for (int i = 0; i < 16; i++) PAL_BG[BP_TITLE * 16 + i] = pal_title[i];
    dma3_copy32(CHARBLOCK(2), title_tiles, sizeof(title_tiles) / 4);
    for (int i = 0; i < 600; i++) SCREENBLOCK(28)[(i / 30) * 32 + (i % 30)] = (u16)(title_map[i] | (BP_TITLE << 12));
    REG_BGCNT(0) = 0 | (1 << 2) | (31 << 8);
    REG_BGCNT(2) = 3 | (2 << 2) | (28 << 8);
    // sprites
    dma3_copy32(OBJ_TILES + OT_PLAYER * 8, spr_player, sizeof(spr_player) / 4);
    for (int k = 0; k < 3; k++) {
        int a = act_mon_arch[ACT - 1][k];
        dma3_copy32(OBJ_TILES + (OT_MON + k * 48) * 8, spr_monsters + a * 48 * 8, 48 * 8);
    }
    dma3_copy32(OBJ_TILES + OT_BOSS * 8, spr_boss, sizeof(spr_boss) / 4);
    dma3_copy32(OBJ_TILES + OT_BIG * 8, spr_big_misc, sizeof(spr_big_misc) / 4);
    dma3_copy32(OBJ_TILES + OT_SMALL * 8, spr_small_misc, sizeof(spr_small_misc) / 4);
    for (int i = 0; i < 16; i++) {
        PAL_OBJ[OP_PLAYER * 16 + i] = pal_player[i];
        PAL_OBJ[OP_BOSS * 16 + i] = pal_boss[ACT - 1][i];
        PAL_OBJ[OP_MISC * 16 + i] = pal_misc[i];
        PAL_OBJ[OP_FLASH * 16 + i] = i ? 0x7FFF : 0;
        {
            u16 c = pal_player[i];
            int r = (c & 31) + 12, g = ((c >> 5) & 31) * 2 / 3, b = ((c >> 10) & 31) * 2 / 3;
            if (r > 31) r = 31;
            PAL_OBJ[OP_HURT * 16 + i] = (u16)(r | (g << 5) | (b << 10));
        }
        for (int k = 0; k < 3; k++) {
            u16 c = pal_act_mon[(ACT - 1) * 3 + k][i];
            PAL_OBJ[(OP_MON + k) * 16 + i] = c;
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            r = r / 3; g = g / 2 + 3; b = b + 14; if (b > 31) b = 31;
            PAL_OBJ[(OP_CHAMP + k) * 16 + i] = (i < 2 || i == 13) ? c : (u16)(r | (g << 5) | (b << 10));
        }
        // red portal: swap the blue ramp for red
        u16 m = pal_misc[i];
        if (i == 2 || i == 3 || i == 4 || i == 14 || i == 15) {
            int r = m & 31, g = (m >> 5) & 31, b = (m >> 10) & 31;
            m = (u16)(b | ((g / 3) << 5) | ((r / 2) << 10));
        }
        PAL_OBJ[OP_PORTAL_RED * 16 + i] = m;
    }
    world_load_gfx();
}

static void hide_sprites(void) { spr_begin(); spr_end(); oam_flush(); }

static void wait_frames(int n) { while (n--) { read_keys(); vsync(); snd_update(); } }

// ---------------------------------------------------------------- menus
static int menu(int x, int y, const char* const* opts, int n, int* cur) {
    // returns index when A pressed, -1 on B
    for (;;) {
        for (int i = 0; i < n; i++) {
            text_put(x, y + i * 2, i == *cur ? ">" : " ", C_GOLD);
            text_put(x + 2, y + i * 2, opts[i], i == *cur ? C_BRIGHT : C_GRAY);
        }
        read_keys(); vsync(); snd_update();
        if (keysNew & KEY_UP) { *cur = (*cur + n - 1) % n; sfx_menu(); }
        if (keysNew & KEY_DOWN) { *cur = (*cur + 1) % n; sfx_menu(); }
        if (keysNew & (KEY_A | KEY_START)) { sfx_menu(); return *cur; }
        if (keysNew & KEY_B) return -1;
    }
}

static void wait_press(u16 mask) {
    for (;;) { read_keys(); vsync(); snd_update(); if (keysNew & mask) return; }
}

static int password_entry(void) {
    char pw[14]; int n = 0, cx = 0, cy = 0;
    for (int i = 0; i < 14; i++) pw[i] = 0;
    text_clear();
    box(1, 1, 28, 18);
    text_center(2, "ENTER PASSWORD", C_GOLD);
    text_center(16, "A:ADD B:DEL START:OK", C_GRAY);
    text_center(17, "SELECT:BACK", C_GRAY);
    for (;;) {
        char shown[20]; int k = 0;
        for (int i = 0; i < 13; i++) { if (i == 4 || i == 8) shown[k++] = '-'; shown[k++] = i < n ? pw[i] : '_'; }
        shown[k] = 0;
        text_center(5, shown, C_YELLOW);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 8; c++) {
                char ch[2] = { B32[r * 8 + c], 0 };
                int sel = (r == cy && c == cx);
                text_put(7 + c * 2 - 1, 8 + r * 2, sel ? ">" : " ", C_GOLD);
                text_put(7 + c * 2, 8 + r * 2, ch, sel ? C_BRIGHT : C_WHITE);
            }
        read_keys(); vsync(); snd_update();
        if (keysNew & KEY_LEFT) cx = (cx + 7) & 7;
        if (keysNew & KEY_RIGHT) cx = (cx + 1) & 7;
        if (keysNew & KEY_UP) cy = (cy + 3) & 3;
        if (keysNew & KEY_DOWN) cy = (cy + 1) & 3;
        if ((keysNew & KEY_A) && n < 13) { pw[n++] = B32[cy * 8 + cx]; sfx_menu(); }
        if ((keysNew & KEY_B) && n > 0) { pw[--n] = 0; sfx_menu(); }
        if (keysNew & KEY_SELECT) return 0;
        if ((keysNew & KEY_START) && n == 13) {
            Hero backup = hero;
            if (!read_password(pw)) {
                hero = backup;
                text_put(2, 13, "                          ", C_RED); text_center(13, "INVALID PASSWORD", C_RED); sfx_hit();
                continue;
            }
            if (hero.act != ACT) {
                char b[30] = "THIS IS FOR ACT ";
                scat(b, romanAct[hero.act - 1]); scat(b, " ROM");
                hero = backup;
                text_put(2, 13, "                          ", C_RED); text_center(13, b, C_RED); sfx_hit();
                continue;
            }
            return 1;
        }
    }
}

static void new_hero(void) {
    hero.diff = 0; hero.act = 1; hero.level = 1; hero.wtier = 0; hero.atier = 0;
    for (int i = 0; i < SK_COUNT; i++) hero.skill[i] = 0;
    hero.skill[SK_SMITE] = 1;
    hero.hpPot = 3; hero.mpPot = 2; hero.deepest = 0; hero.xp = 0; hero.gold = 50;
}

// returns 1 when a hero is ready to play this act
static int title_and_menu(void) {
    REG_DISPCNT = 0x0500;   // BG0 + BG2
    light_update(0);
    REG_BLDCNT = 0;
    text_clear();
    hide_sprites();
    int blink = 0;
    for (;;) {
        if ((blink++ >> 5) & 1) text_clear_row(14); else text_center(14, "PRESS START", C_WHITE);
        {
            char b[24] = "ACT "; scat(b, romanAct[ACT - 1]); scat(b, " CARTRIDGE");
            text_center(17, b, C_GOLD);
        }
        text_center(18, "ORIGINAL FAN TRIBUTE", C_GRAY);
        read_keys(); vsync(); snd_update();
        if (keysNew & (KEY_START | KEY_A)) break;
    }
    sfx_menu();
    int cur = 0;
    for (;;) {
        text_clear();
        box(3, 9, 26, 18);
        text_center(10, actNames[ACT - 1], C_GOLD);
        int hasSave = save_read();
        const char* opts[3]; int ids[3]; int n = 0;
        if (hasSave) { opts[n] = "CONTINUE"; ids[n++] = 0; }
        if (ACT == 1) { opts[n] = "NEW GAME"; ids[n++] = 1; }
        opts[n] = "PASSWORD"; ids[n++] = 2;
        if (cur >= n) cur = 0;
        int r = menu(8, 12, opts, n, &cur);
        if (r < 0) return 0;
        int id = ids[r];
        if (id == 0) {
            save_read();
            if (hero.act == ACT) return 1;
            // save belongs to another act cartridge: show where to go
            text_clear();
            box(1, 4, 28, 15);
            char b[30] = "CONTINUE ON ACT "; scat(b, romanAct[hero.act - 1]); scat(b, " ROM");
            text_center(6, b, C_GOLD);
            text_center(8, diffNames[hero.diff], hero.diff ? C_RED : C_WHITE);
            text_center(10, "PASSWORD:", C_GRAY);
            show_password(12);
            text_center(14, "PRESS A", C_GRAY);
            wait_press(KEY_A | KEY_B | KEY_START);
        } else if (id == 1) {
            if (hasSave) {
                text_clear(); box(3, 6, 26, 13);
                text_center(8, "OVERWRITE SAVE?", C_RED);
                text_center(10, "A: YES   B: NO", C_WHITE);
                for (;;) { read_keys(); vsync(); if (keysNew & (KEY_A | KEY_B)) break; }
                if (!(keysNew & KEY_A)) continue;
            }
            new_hero();
            save_write();
            return 1;
        } else {
            if (password_entry()) { save_write(); return 1; }
        }
    }
}

// ---------------------------------------------------------------- in-game menus
static void enter_floor(int floor) {
    REG_DISPCNT = 0x1340;
    text_clear();
    hide_sprites();
    if (floor > hero.deepest && floor > 0) hero.deepest = floor;
    play_init_floor(floor, 0);
    camera_update();
    map_scroll_update();
    show_msg(floorNames[ACT - 1][floor], floor ? C_GRAY : C_GREEN);
    if (floor == 0 || floor == hero.deepest) save_write();
    portalLock = 40;
    sfx_portal();
}

static void pause_menu(void) {
    int cur = 0;
    for (;;) {
        text_clear();
        box(0, 0, 29, 19);
        char b[32];
        b[0] = 0; scat(b, diffNames[hero.diff]); scat(b, "  ACT "); scat(b, romanAct[ACT - 1]);
        text_center(1, b, hero.diff ? C_RED : C_GOLD);
        text_put(2, 3, "LEVEL", C_GRAY); text_num(8, 3, hero.level, 0, C_WHITE);
        text_put(12, 3, "NEXT", C_GRAY); text_num(17, 3, xp_need(hero.level) - hero.xp, 0, C_WHITE);
        text_put(2, 4, "LIFE", C_GRAY); text_num(7, 4, pl.hp, 0, C_RED); text_put(11, 4, "/", C_GRAY); text_num(12, 4, pl.maxhp, 0, C_RED);
        text_put(17, 4, "MANA", C_GRAY); text_num(22, 4, pl.mp, 0, C_BLUE); text_put(25, 4, "/", C_GRAY); text_num(26, 4, pl.maxmp, 0, C_BLUE);
        text_put(2, 5, "DAMAGE", C_GRAY); text_num(9, 5, base_damage(), 0, C_WHITE);
        text_put(15, 5, "ARMOR", C_GRAY); text_num(21, 5, hero.atier * 9 + hero.level * 2, 0, C_WHITE);
        text_put(2, 6, "W", C_GOLD); text_put(4, 6, weapon_name(hero.wtier), C_WHITE);
        text_put(2, 7, "A", C_GOLD); text_put(4, 7, armor_name(hero.atier), C_WHITE);
        text_put(2, 9, "SKILL POINTS", C_GRAY); text_num(15, 9, skill_points(), 0, skill_points() ? C_YELLOW : C_WHITE);
        const char* opts[6];
        static char lines[4][24];
        for (int i = 0; i < 4; i++) {
            lines[i][0] = 0; scat(lines[i], skillNames[i]);
            int L = slen(lines[i]); while (L < 13) lines[i][L++] = ' ';
            lines[i][L] = 0;
            char nb[4]; itoa_buf(nb, hero.skill[i]); scat(lines[i], nb);
            opts[i] = lines[i];
        }
        opts[4] = curFloor ? "TOWN PORTAL" : "-";
        opts[5] = "RESUME";
        text_put(3, 18, "A:LEARN  START:BACK", C_GRAY);
        // custom single-column menu
        int done = 0;
        while (!done) {
            for (int i = 0; i < 6; i++) {
                text_put(2, 10 + i + (i >= 4 ? 1 : 0), i == cur ? ">" : " ", C_GOLD);
                text_put(4, 10 + i + (i >= 4 ? 1 : 0), opts[i], i == cur ? C_BRIGHT : C_GRAY);
            }
            read_keys(); vsync(); snd_update();
            if (keysNew & KEY_UP) { cur = (cur + 5) % 6; sfx_menu(); }
            if (keysNew & KEY_DOWN) { cur = (cur + 1) % 6; sfx_menu(); }
            if (keysNew & (KEY_START | KEY_B)) { text_clear(); return; }
            if (keysNew & KEY_A) {
                if (cur < 4) {
                    if (skill_points() > 0 && hero.skill[cur] < 15) {
                        hero.skill[cur]++;
                        if (cur < 3 && hero.skill[pl.cur] == 0) pl.cur = cur;
                        player_recalc(); sfx_levelup(); done = 1;
                    } else sfx_hit();
                } else if (cur == 4 && curFloor) {
                    text_clear(); enter_floor(0); return;
                } else if (cur == 5) { text_clear(); return; }
            }
        }
    }
}

static void healer_menu(void) {
    pl.hp = pl.maxhp; pl.mp = pl.maxmp;
    sfx_potion();
    int cur = 0;
    int price = 15 * ACT * (hero.diff + 1);
    for (;;) {
        text_clear();
        box(1, 5, 28, 16);
        text_center(6, "SISTER MAREN", C_GOLD);
        text_center(7, "YOUR WOUNDS ARE MENDED.", C_WHITE);
        char o1[30] = "HEALING POTION $", o2[30] = "MANA POTION    $";
        char nb[8]; itoa_buf(nb, price); scat(o1, nb); scat(o2, nb);
        const char* opts[3] = { o1, o2, "LEAVE" };
        char g[16] = "GOLD $"; itoa_buf(nb, hero.gold); scat(g, nb);
        text_center(15, g, C_GOLD);
        int r = menu(4, 9, opts, 3, &cur);
        if (r < 0 || r == 2) break;
        u8* pot = r == 0 ? &hero.hpPot : &hero.mpPot;
        if (hero.gold >= (u32)price && *pot < 9) { hero.gold -= price; (*pot)++; sfx_pickup(); }
        else sfx_hit();
    }
    text_clear();
    healerLock = 30;
}

static void death_screen(void) {
    wait_frames(50);
    text_clear();
    box(3, 6, 26, 13);
    text_center(8, "YOU HAVE DIED", C_RED);
    text_center(10, "LOST 10% GOLD", C_GRAY);
    text_center(12, "PRESS A", C_WHITE);
    wait_press(KEY_A | KEY_START);
    hero.gold -= hero.gold / 10;
    pl.hp = pl.maxhp; pl.mp = pl.maxmp; pl.dead = 0;
    enter_floor(0);
}

void on_act_clear(void) {
    light_update(0);
    hide_sprites();
    REG_DISPCNT = 0x0500;
    text_clear();
    int finished = (ACT == 5 && hero.diff == 2);
    hero.deepest = 0;
    if (ACT < 5) hero.act = ACT + 1;
    else { hero.act = 1; if (hero.diff < 2) hero.diff++; }
    save_write();
    box(1, 2, 28, 18);
    char b[32] = "ACT "; scat(b, romanAct[ACT - 1]); scat(b, " COMPLETE");
    text_center(3, b, C_GOLD);
    if (finished) {
        text_center(5, "THE ABYSS IS SEALED.", C_WHITE);
        text_center(6, "YOU CONQUERED HELL!", C_YELLOW);
    } else if (ACT == 5) {
        text_center(5, "A DARKER CYCLE BEGINS:", C_WHITE);
        text_center(6, diffNames[hero.diff], C_RED);
    } else {
        text_center(5, "THE JOURNEY CONTINUES", C_WHITE);
    }
    char n[32] = "NEXT: ACT "; scat(n, romanAct[hero.act - 1]); scat(n, " ROM");
    text_center(8, n, C_GOLD);
    text_center(10, "PASSWORD", C_GRAY);
    show_password(12);
    text_center(14, "PROGRESS SAVED", C_GREEN);
    text_center(16, "PRESS START", C_WHITE);
    sfx_levelup();
    wait_press(KEY_START);
}

// ---------------------------------------------------------------- main
int main(void) {
    REG_WAITCNT = 0x4317;
    REG_DISPCNT = 0x0080;   // forced blank while loading
    snd_init();
    load_common_gfx();
    light_init();
    for (;;) {
        if (!title_and_menu()) continue;
        player_recalc();
        pl.hp = pl.maxhp; pl.mp = pl.maxmp; pl.cur = SK_SMITE;
        bossDefeated = 0;
        enter_floor(0);
        int running = 1;
        while (running) {
            read_keys();
            if (keysNew & KEY_START) { pause_menu(); continue; }
            // healer interaction in town
            if (curFloor == 0 && (keysNew & KEY_A) && !healerLock) {
                s32 dx = pl.x - healerX, dy = pl.y - healerY;
                if (dx < 400 && dx > -400 && dy < 400 && dy > -400) { healer_menu(); continue; }
            }
            if (healerLock) healerLock--;
            play_update();
            // portals
            if (portalLock) portalLock--;
            else {
                s32 dx = pl.x - portalBlueX, dy = pl.y - portalBlueY;
                if (dx < 150 && dx > -150 && dy < 150 && dy > -150) {
                    if (curFloor == 0) enter_floor(hero.deepest ? hero.deepest : 1);
                    else enter_floor(0);
                    continue;
                }
                if (portalRedOn || (curFloor == 3 && bossDefeated)) {
                    dx = pl.x - portalRedX; dy = pl.y - portalRedY;
                    if (dx < 150 && dx > -150 && dy < 150 && dy > -150) {
                        if (curFloor < 3) { enter_floor(curFloor + 1); continue; }
                        on_act_clear();
                        running = 0;
                        continue;
                    }
                }
            }
            play_draw();
            vsync();
            oam_flush();
            map_scroll_update();
            light_update(curFloor > 0);
            hud_draw();
            snd_update();
            if (pl.dead) death_screen();
        }
    }
}
