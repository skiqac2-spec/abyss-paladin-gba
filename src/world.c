// World: dungeon generation, isometric BG rendering, camera, torch light
#include "game.h"

u8 map[MAPW * MAPH];
int camx, camy;

typedef struct { u8 x, y, w, h; } Room;
Room rooms[12];
int nrooms;

s32 portalBlueX, portalBlueY, portalRedX, portalRedY;
int portalRedOn;
s32 healerX, healerY, fireX, fireY;

static int lastGX = -10000, lastGY = -10000;
static u16 winTable[161];
static u8 halfW[64];

int tile_at(int i, int j) {
    if ((unsigned)i >= MAPW || (unsigned)j >= MAPH) return T_VOID;
    return map[i * MAPH + j];
}

static void set_tile(int i, int j, int t) {
    if ((unsigned)i < MAPW && (unsigned)j < MAPH) map[i * MAPH + j] = t;
}

int walkable(s32 x, s32 y) {
    if (x < 0 || y < 0) return 0;
    int t = tile_at(x >> 8, y >> 8);
    return t == T_FLOOR || t == T_FLOOR2;
}

void world_to_screen(s32 x, s32 y, int* sx, int* sy) {
    *sx = (((x - y) * 16) >> 8) - camx;
    *sy = (((x + y) * 8) >> 8) - camy;
}

void world_load_gfx(void) {
    dma3_copy32(CHARBLOCK(0), iso_tiles, sizeof(iso_tiles) / 4);
    for (int i = 0; i < 16; i++) PAL_BG[BP_MAP * 16 + i] = pal_act_bg[ACT - 1][i];
    REG_BGCNT(1) = 2 | (0 << 2) | (30 << 8);
    lastGX = -10000;
}

// ---------------------------------------------------------------- generation
static void carve_room(int x, int y, int w, int h, int t) {
    for (int i = x; i < x + w; i++)
        for (int j = y; j < y + h; j++) set_tile(i, j, t);
}

static void carve_corridor(int x0, int y0, int x1, int y1) {
    int x = x0, y = y0;
    int horizFirst = rndr(2);
    while (x != x1 || y != y1) {
        set_tile(x, y, T_FLOOR); set_tile(x + 1, y, T_FLOOR);
        set_tile(x, y + 1, T_FLOOR); set_tile(x + 1, y + 1, T_FLOOR);
        if ((horizFirst && x != x1) || y == y1) x += (x1 > x) ? 1 : -1;
        else y += (y1 > y) ? 1 : -1;
    }
    set_tile(x, y, T_FLOOR);
}

static void add_walls(void) {
    for (int i = 0; i < MAPW; i++)
        for (int j = 0; j < MAPH; j++) {
            if (tile_at(i, j) != T_VOID) continue;
            int near = 0;
            for (int di = -1; di <= 1 && !near; di++)
                for (int dj = -1; dj <= 1; dj++) {
                    int t = tile_at(i + di, j + dj);
                    if (t == T_FLOOR || t == T_FLOOR2) { near = 1; break; }
                }
            if (near) set_tile(i, j, T_WALL);
        }
}

static void sprinkle_floor2(int count) {
    for (int k = 0; k < count; k++) {
        int ci = 2 + rndr(MAPW - 4), cj = 2 + rndr(MAPH - 4);
        int r = 1 + rndr(3);
        for (int i = ci - r; i <= ci + r; i++)
            for (int j = cj - r; j <= cj + r; j++)
                if (tile_at(i, j) == T_FLOOR && rndr(4)) set_tile(i, j, T_FLOOR2);
    }
}

static int overlaps(int x, int y, int w, int h) {
    for (int k = 0; k < nrooms; k++) {
        Room* r = &rooms[k];
        if (x < r->x + r->w + 2 && x + w + 2 > r->x && y < r->y + r->h + 2 && y + h + 2 > r->y) return 1;
    }
    return 0;
}

static void gen_town(void) {
    for (int i = 0; i < MAPW * MAPH; i++) map[i] = T_VOID;
    carve_room(12, 12, 24, 24, T_FLOOR2);
    carve_room(22, 12, 4, 24, T_FLOOR);
    carve_room(12, 22, 24, 4, T_FLOOR);
    carve_room(20, 20, 8, 8, T_FLOOR);
    // ruined pillars for atmosphere
    const u8 pil[][2] = { {15, 15}, {15, 16}, {16, 15}, {32, 15}, {32, 16}, {15, 32}, {32, 32}, {31, 32}, {18, 30}, {30, 18} };
    for (unsigned k = 0; k < sizeof(pil) / 2; k++) set_tile(pil[k][0], pil[k][1], T_WALL);
    add_walls();
    nrooms = 1;
    rooms[0].x = 12; rooms[0].y = 12; rooms[0].w = 24; rooms[0].h = 24;
    fireX = 24 * FX; fireY = 24 * FX;
    healerX = 20 * FX + 128; healerY = 26 * FX + 128;
    portalBlueX = 29 * FX + 128; portalBlueY = 23 * FX + 128;
    portalRedOn = 0;
}

static void gen_dungeon(int floor) {
    for (int i = 0; i < MAPW * MAPH; i++) map[i] = T_VOID;
    nrooms = 0;
    int bossRoom = (floor == 3);
    // big room first for the boss, swapped to the end afterwards
    if (bossRoom) {
        int w = 11, h = 11;
        int x = rndr(2) ? 3 : MAPW - 3 - w;
        int y = rndr(2) ? 3 : MAPH - 3 - h;
        rooms[0].x = x; rooms[0].y = y; rooms[0].w = w; rooms[0].h = h;
        nrooms = 1;
    }
    for (int tries = 0; tries < 300 && nrooms < 10; tries++) {
        int w = 4 + rndr(5), h = 4 + rndr(5);
        int x = 3 + rndr(MAPW - 6 - w), y = 3 + rndr(MAPH - 6 - h);
        if (overlaps(x, y, w, h)) continue;
        rooms[nrooms].x = x; rooms[nrooms].y = y; rooms[nrooms].w = w; rooms[nrooms].h = h;
        nrooms++;
    }
    if (bossRoom) { Room b = rooms[0]; rooms[0] = rooms[nrooms - 1]; rooms[nrooms - 1] = b; }
    // order rooms as a chain by nearest neighbour to make corridors short
    for (int k = 1; k < nrooms - (bossRoom ? 1 : 0); k++) {
        int best = k, bd = 1 << 30;
        for (int m = k; m < nrooms - (bossRoom ? 1 : 0); m++) {
            int dx = rooms[m].x - rooms[k - 1].x, dy = rooms[m].y - rooms[k - 1].y;
            int d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = m; }
        }
        Room t = rooms[k]; rooms[k] = rooms[best]; rooms[best] = t;
    }
    for (int k = 0; k < nrooms; k++) carve_room(rooms[k].x, rooms[k].y, rooms[k].w, rooms[k].h, T_FLOOR);
    for (int k = 1; k < nrooms; k++) {
        Room* a = &rooms[k - 1]; Room* b = &rooms[k];
        carve_corridor(a->x + a->w / 2, a->y + a->h / 2, b->x + b->w / 2, b->y + b->h / 2);
    }
    sprinkle_floor2(10);
    // a few broken pillars inside larger rooms
    for (int k = 1; k < nrooms - 1; k++) {
        Room* r = &rooms[k];
        if (r->w >= 7 && r->h >= 7 && rndr(2)) set_tile(r->x + 2, r->y + 2, T_WALL);
    }
    add_walls();
    Room* s = &rooms[0];
    Room* e = &rooms[nrooms - 1];
    portalBlueX = (s->x + 1) * FX + 128; portalBlueY = (s->y + 1) * FX + 128;
    portalRedX = (e->x + e->w / 2) * FX + 128; portalRedY = (e->y + e->h / 2) * FX + 128;
    portalRedOn = (floor < 3);
}

void gen_floor(int floor) {
    if (floor == 0) gen_town();
    else gen_dungeon(floor);
}

// ---------------------------------------------------------------- rendering
static inline int lat_type(int a, int b) {
    // lattice (a,b) with a+b even -> world tile
    return tile_at((a + b) >> 1, (b - a) >> 1);
}

static u16 cell_tile(int gx, int gy) {
    int rx = gx >> 1, ry = gy, side = gx & 1;
    int orient = (rx + ry) & 1;
    int tT, tB, tX;
    if (!orient) { tT = lat_type(rx, ry); tB = lat_type(rx + 1, ry + 1); tX = lat_type(rx + 1, ry - 1); }
    else { tT = lat_type(rx + 1, ry); tB = lat_type(rx, ry + 1); tX = lat_type(rx, ry - 1); }
    return (u16)(((((orient * 2 + side) * 4 + tT) * 4 + tB) * 2) + (tX == T_WALL ? 1 : 0));
}

static void draw_cell(int gx, int gy) {
    SCREENBLOCK(30)[((gy & 31) << 5) | (gx & 31)] = cell_tile(gx, gy);
}

void map_redraw_full(void) {
    int gx0 = camx >> 3, gy0 = (camy - 8) >> 3;
    for (int y = 0; y < 22; y++)
        for (int x = 0; x < 31; x++) draw_cell(gx0 + x, gy0 + y);
    lastGX = gx0; lastGY = gy0;
}

void map_scroll_update(void) {
    int gx0 = camx >> 3, gy0 = (camy - 8) >> 3;
    int dx = gx0 - lastGX, dy = gy0 - lastGY;
    if (dx == 0 && dy == 0) goto scroll;
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1) { map_redraw_full(); goto scroll; }
    if (dx > 0) for (int y = 0; y < 22; y++) draw_cell(gx0 + 30, gy0 + y);
    if (dx < 0) for (int y = 0; y < 22; y++) draw_cell(gx0, gy0 + y);
    if (dy > 0) for (int x = 0; x < 31; x++) draw_cell(gx0 + x, gy0 + 21);
    if (dy < 0) for (int x = 0; x < 31; x++) draw_cell(gx0 + x, gy0);
    lastGX = gx0; lastGY = gy0;
scroll:
    REG_BGHOFS(1) = camx;
    REG_BGVOFS(1) = camy - 8;
}

void camera_update(void) {
    int sx = ((pl.x - pl.y) * 16) >> 8;
    int sy = ((pl.x + pl.y) * 8) >> 8;
    camx = sx - 120;
    camy = sy - 74;
}

// ---------------------------------------------------------------- torch light (HDMA window)
static int isqrt(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

void light_init(void) {
    const int RX = 86, RY = 62;
    for (int dy = 0; dy < 64; dy++) {
        if (dy >= RY) { halfW[dy] = 0; continue; }
        halfW[dy] = (u8)(RX * isqrt(RY * RY - dy * dy) / RY);
    }
}

void light_update(int on) {
    REG_DMA0CNT_H = 0;
    if (!on) {
        REG_DISPCNT &= ~0x2000;
        REG_BLDCNT = 0;
        return;
    }
    int cx = (((pl.x - pl.y) * 16) >> 8) - camx;
    int cy = (((pl.x + pl.y) * 8) >> 8) - camy - 12;
    for (int y = 0; y < 161; y++) {
        int d = y - cy; if (d < 0) d = -d;
        u16 v = 0;
        if (d < 64 && halfW[d]) {
            int l = cx - halfW[d], r = cx + halfW[d];
            if (l < 0) l = 0;
            if (r > 240) r = 240;
            if (r > l) v = (u16)((l << 8) | r);
        }
        winTable[y] = v;
    }
    REG_WIN0H = winTable[0];
    REG_WIN0V = 160;
    REG_WININ = 0x13;             // inside: BG0, BG1, OBJ, no effect
    REG_WINOUT = 0x33;            // outside: + blend effect (darken)
    REG_BLDCNT = 0x00F2;          // darken BG1, OBJ, backdrop
    REG_BLDY = 11;
    REG_DMA0SAD = (u32)&winTable[1];
    REG_DMA0DAD = (u32)&REG_WIN0H;
    REG_DMA0CNT_L = 1;
    REG_DMA0CNT_H = 0x8000 | 0x2000 | 0x0200 | 0x0040;   // enable, hblank, repeat, dest fixed, 16-bit
    REG_DISPCNT |= 0x2000;
}
