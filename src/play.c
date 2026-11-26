// Gameplay: paladin, monsters, skills, loot, sprite rendering
#include "game.h"

Player pl;
Mon mons[MAX_MON];
Proj projs[MAX_PROJ];
Item items[MAX_ITEM];
int bossAlive, bossDefeated;

typedef struct { s16 x, y; u8 t; } Spark;
static Spark sparks[MAX_SPARK];
extern struct { u8 x, y, w, h; } rooms[12];
extern int nrooms;

// world velocity per screen direction (0=S,1=SE,2=E,3=NE,4=N,5=NW,6=W,7=SW), unit = 1/256 tile
static const s8 dirVX[8] = { 13, 18, 13, 0, -13, -18, -13, 0 };
static const s8 dirVY[8] = { 13, 0, -13, -18, -13, 0, 13, 18 };
// sprite frame direction and flip for each screen direction
static const u8 dirFrame[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
static const u8 dirFlip[8] = { 0, 0, 0, 0, 0, 1, 1, 1 };

// monster base stats by archetype: imp, skeleton, zombie, beast, spider
static const u8 archHP[5] = { 14, 20, 28, 18, 16 };
static const u8 archDmg[5] = { 3, 4, 6, 4, 3 };
static const u8 archSpd[5] = { 11, 8, 5, 13, 11 };
static const u8 archXP[5] = { 6, 8, 9, 8, 7 };
// per difficulty x act multipliers (x10)
static const u16 hpMul[3][5] = { {10, 24, 44, 70, 100}, {110, 140, 175, 215, 260}, {300, 360, 430, 510, 600} };
static const u16 dmgMul[3][5] = { {10, 17, 26, 36, 48}, {45, 55, 66, 78, 92}, {90, 105, 120, 138, 158} };
static const u16 xpMul[3][5] = { {10, 24, 44, 70, 100}, {120, 150, 185, 225, 270}, {320, 380, 450, 530, 620} };

// ---------------------------------------------------------------- sprites / OAM
typedef struct { s16 depth; u16 a0, a1, a2; } SprEnt;
static SprEnt sl[128];
static int sn;
static u16 oamShadow[128 * 4];

void spr_begin(void) { sn = 0; }

void spr_add(int x, int y, int depth, u16 a0, u16 a1, u16 a2) {
    if (sn >= 120) return;
    if (x < -64 || x > 240 || y < -64 || y > 160) return;
    sl[sn].depth = depth;
    sl[sn].a0 = (a0 & 0xFF00) | (y & 0xFF);
    sl[sn].a1 = (a1 & 0xFE00) | (x & 0x1FF);
    sl[sn].a2 = a2;
    sn++;
}

void spr_end(void) {
    // insertion sort by depth descending (front-most first = highest priority)
    for (int i = 1; i < sn; i++) {
        SprEnt e = sl[i]; int j = i - 1;
        while (j >= 0 && sl[j].depth < e.depth) { sl[j + 1] = sl[j]; j--; }
        sl[j + 1] = e;
    }
    for (int i = 0; i < 128; i++) {
        if (i < sn) {
            oamShadow[i * 4] = sl[i].a0; oamShadow[i * 4 + 1] = sl[i].a1; oamShadow[i * 4 + 2] = sl[i].a2;
        } else {
            oamShadow[i * 4] = 0x0200; oamShadow[i * 4 + 1] = 0; oamShadow[i * 4 + 2] = 0;
        }
    }
}

void oam_flush(void) { dma3_copy32(OAM, oamShadow, 256); }

#define SQ32 (0x0000), (0x8000)
#define A2(tile, pal) ((u16)((tile) | (1 << 10) | ((pal) << 12)))

static void add32(s32 wx, s32 wy, int tile, int pal, int flip, int lift) {
    int sx, sy; world_to_screen(wx, wy, &sx, &sy);
    spr_add(sx - 16, sy - 30 - lift, sy, 0x0000, 0x8000 | (flip ? 0x1000 : 0), A2(tile, pal));
}
static void add16(s32 wx, s32 wy, int tile, int pal, int lift) {
    int sx, sy; world_to_screen(wx, wy, &sx, &sy);
    spr_add(sx - 8, sy - 12 - lift, sy - 1, 0x0000, 0x4000, A2(tile, pal));
}

// ---------------------------------------------------------------- helpers
static int iabs(int v) { return v < 0 ? -v : v; }
static int dist_approx(s32 dx, s32 dy) {
    dx = iabs(dx); dy = iabs(dy);
    return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

static void spark_at(s32 x, s32 y) {
    for (int i = 0; i < MAX_SPARK; i++)
        if (!sparks[i].t) {
            int sx, sy; world_to_screen(x, y, &sx, &sy);
            sparks[i].x = sx + camx; sparks[i].y = sy + camy; sparks[i].t = 10; return;
        }
}

static void move_entity(s32* x, s32* y, s32 vx, s32 vy, int r) {
    s32 nx = *x + vx;
    if (walkable(nx + (vx > 0 ? r : -r), *y - r) && walkable(nx + (vx > 0 ? r : -r), *y + r)) *x = nx;
    s32 ny = *y + vy;
    if (walkable(*x - r, ny + (vy > 0 ? r : -r)) && walkable(*x + r, ny + (vy > 0 ? r : -r))) *y = ny;
}

void player_recalc(void) {
    pl.maxhp = hero_maxhp();
    pl.maxmp = hero_maxmp();
    if (pl.hp > pl.maxhp) pl.hp = pl.maxhp;
    if (pl.mp > pl.maxmp) pl.mp = pl.maxmp;
}

void give_xp(u32 xp) {
    if (hero.level >= 99) return;
    hero.xp += xp;
    int leveled = 0;
    while (hero.level < 99 && hero.xp >= xp_need(hero.level)) {
        hero.xp -= xp_need(hero.level);
        hero.level++;
        leveled = 1;
    }
    if (leveled) {
        player_recalc();
        pl.hp = pl.maxhp; pl.mp = pl.maxmp;
        show_msg("LEVEL UP! +1 SKILL POINT (START)", C_YELLOW);
        sfx_levelup();
    }
}

// ---------------------------------------------------------------- spawning
static int monster_slot(void) {
    for (int i = 0; i < MAX_MON; i++) if (!mons[i].active) return i;
    return -1;
}

static void spawn_mon(int kind, s32 x, s32 y, int champ) {
    int s = monster_slot();
    if (s < 0 || !walkable(x, y)) return;
    Mon* m = &mons[s];
    int a = act_mon_arch[ACT - 1][kind < 3 ? kind : 0];
    int fl = curFloor > 0 ? curFloor : 1;
    int hp, dmg, xp;
    if (kind == 3) {
        hp = 260; dmg = 9; xp = 120;
    } else {
        hp = archHP[a]; dmg = archDmg[a]; xp = archXP[a];
    }
    hp = hp * hpMul[hero.diff][ACT - 1] / 10 * (10 + fl * 2) / 10;
    dmg = dmg * dmgMul[hero.diff][ACT - 1] / 10 * (10 + fl) / 10;
    xp = xp * xpMul[hero.diff][ACT - 1] / 10;
    if (champ) { hp = hp * 5 / 2; dmg = dmg * 3 / 2; xp *= 3; }
    if (hp > 32000) hp = 32000;
    m->active = 1; m->kind = kind; m->x = x; m->y = y;
    m->hp = m->maxhp = hp; m->dmg = dmg; m->xp = xp; m->champ = champ;
    m->state = 0; m->timer = 0; m->cd = 30; m->stun = 0; m->flash = 0;
    m->speed = kind == 3 ? 7 : archSpd[a] + (champ ? 2 : 0);
    m->anim = rndr(32); m->face = 0;
}

static void spawn_monsters(void) {
    for (int i = 0; i < MAX_MON; i++) mons[i].active = 0;
    bossAlive = 0;
    if (curFloor == 0) return;
    int last = nrooms - 1;
    for (int r = 1; r < nrooms; r++) {
        int packs = (r == last && curFloor == 3) ? 0 : 1 + rndr(2);
        for (int p = 0; p < packs; p++) {
            int kind = rndr(3);
            int champ = rndr(100) < 12;
            int n = 2 + rndr(3);
            int cx = rooms[r].x + 1 + rndr(rooms[r].w - 2), cy = rooms[r].y + 1 + rndr(rooms[r].h - 2);
            for (int k = 0; k < n; k++)
                spawn_mon(kind, (cx + rndr(3) - 1) * FX + 128, (cy + rndr(3) - 1) * FX + 128, champ);
        }
    }
    if (curFloor == 3 && !bossDefeated) {
        s32 bx = (rooms[last].x + rooms[last].w / 2) * FX + 128;
        s32 by = (rooms[last].y + rooms[last].h / 2) * FX + 128;
        spawn_mon(3, bx, by, 0);
        for (int k = 0; k < 3; k++) spawn_mon(rndr(3), bx + (k - 1) * 384, by + 512, 1);
        bossAlive = 1;
    }
}

void play_init_floor(int floor, int fromTown) {
    curFloor = floor;
    for (int i = 0; i < MAX_PROJ; i++) projs[i].active = 0;
    for (int i = 0; i < MAX_ITEM; i++) items[i].active = 0;
    for (int i = 0; i < MAX_SPARK; i++) sparks[i].t = 0;
    // deterministic layout per act / difficulty / floor, but fresh each visit
    gen_floor(floor);
    if (floor == 0) { pl.x = 23 * FX + 128; pl.y = 27 * FX + 128; }
    else { pl.x = portalBlueX + 384; pl.y = portalBlueY + 384; if (!walkable(pl.x, pl.y)) { pl.x = portalBlueX; pl.y = portalBlueY + 256; } }
    if (!walkable(pl.x, pl.y)) { pl.x = (rooms[0].x + 2) * FX + 128; pl.y = (rooms[0].y + 2) * FX + 128; }
    pl.attacking = 0; pl.hurt = 60; pl.dir = 0; pl.dead = 0;
    spawn_monsters();
    camera_update();
    map_redraw_full();
    (void)fromTown;
}

// ---------------------------------------------------------------- loot
static void drop_item(s32 x, s32 y, int kind, int tier, int quality, int value) {
    for (int i = 0; i < MAX_ITEM; i++)
        if (!items[i].active) {
            Item* it = &items[i];
            it->active = 1; it->kind = kind; it->tier = tier; it->quality = quality; it->value = value;
            it->x = x + (rndr(160) - 80); it->y = y + (rndr(160) - 80);
            if (!walkable(it->x, it->y)) { it->x = x; it->y = y; }
            return;
        }
}

static void roll_loot(Mon* m) {
    int r = rndr(100);
    int boss = m->kind == 3;
    int rolls = boss ? 6 : (m->champ ? 2 : 1);
    for (int k = 0; k < rolls; k++) {
        r = rndr(100);
        if (r < 38) drop_item(m->x, m->y, 2, 0, 0, (u16)((3 + rndr(6)) * ACT * (hero.diff + 1) * (boss ? 4 : 1)));
        else if (r < 52) drop_item(m->x, m->y, 0, 0, 0, 0);
        else if (r < 62) drop_item(m->x, m->y, 1, 0, 0, 0);
        else if (r < 69 || (boss && k < 2)) {
            int q = rndr(100);
            int quality = q < 3 ? 3 : q < 15 ? 2 : q < 45 ? 1 : 0;
            if (boss && quality < 2) quality = 2;
            int base = (ACT - 1) * 2 + hero.diff * 10 + (curFloor > 1 ? 1 : 0) + rndr(2);
            int tier = base + quality;
            if (tier > 31) tier = 31;
            drop_item(m->x, m->y, rndr(2) ? 3 : 4, tier, quality, 0);
        }
    }
}

static void pickup_items(void) {
    for (int i = 0; i < MAX_ITEM; i++) {
        Item* it = &items[i];
        if (!it->active) continue;
        if (dist_approx(it->x - pl.x, it->y - pl.y) > 150) continue;
        char b[40];
        switch (it->kind) {
        case 0: if (hero.hpPot >= 9) continue; hero.hpPot++; show_msg("HEALING POTION", C_RED); break;
        case 1: if (hero.mpPot >= 9) continue; hero.mpPot++; show_msg("MANA POTION", C_BLUE); break;
        case 2: {
            hero.gold += it->value; if (hero.gold > 40000) hero.gold = 40000;
            int n = 0; b[n++] = '+'; n += itoa_buf(b + n, it->value);
            const char* g = " GOLD"; while (*g) b[n++] = *g++; b[n] = 0;
            show_msg(b, C_GOLD); break; }
        default: {
            static const u8 qcol[4] = { C_WHITE, C_BLUE, C_YELLOW, C_GOLD };
            static const char* const qn[4] = { "", "MAGIC ", "RARE ", "UNIQUE " };
            const char* nm = it->kind == 3 ? weapon_name(it->tier) : armor_name(it->tier);
            int better = it->kind == 3 ? it->tier > hero.wtier : it->tier > hero.atier;
            int n = 0; const char* p = qn[it->quality]; while (*p) b[n++] = *p++;
            p = nm; while (*p && n < 26) b[n++] = *p++;
            if (better) {
                if (it->kind == 3) hero.wtier = it->tier; else hero.atier = it->tier;
                p = " EQUIP"; while (*p) b[n++] = *p++;
                player_recalc();
            } else {
                int g = 10 + it->tier * 8; hero.gold += g;
                p = " SOLD"; while (*p) b[n++] = *p++;
            }
            b[n] = 0;
            show_msg(b, qcol[it->quality]);
        } }
        it->active = 0;
        sfx_pickup();
    }
}

// ---------------------------------------------------------------- combat
static void hurt_mon(Mon* m, int dmg, int stun) {
    if (m->state == 3) return;
    m->hp -= dmg;
    m->flash = 4;
    if (stun && m->kind != 3) m->stun = stun;
    spark_at(m->x, m->y);
    sfx_hit();
    if (m->hp <= 0) {
        m->state = 3; m->timer = 24;
        give_xp(m->xp);
        roll_loot(m);
        if (m->kind == 3) {
            bossAlive = 0; bossDefeated = 1;
            show_msg("THE GUARDIAN HAS FALLEN!", C_GOLD);
            sfx_boss();
        }
    } else if (m->state == 0) m->state = 1;
}

static int is_undead(Mon* m) {
    if (m->kind == 3) return 0;
    int a = act_mon_arch[ACT - 1][m->kind];
    return a == 1 || a == 2;
}

static void melee_hit(int mult, int stun) {
    s32 fx = pl.x + dirVX[pl.dir] * 4, fy = pl.y + dirVY[pl.dir] * 4;
    int hit = 0;
    for (int i = 0; i < MAX_MON; i++) {
        Mon* m = &mons[i];
        if (!m->active || m->state == 3) continue;
        int reach = m->kind == 3 ? 420 : 300;
        if (dist_approx(m->x - fx, m->y - fy) < reach) {
            hurt_mon(m, player_damage_roll() * mult / 100, stun);
            hit = 1;
        }
    }
    if (!hit) sfx_swing();
}

static void cast_skill(void) {
    int sk = pl.cur;
    int lv = hero.skill[sk];
    if (lv == 0 || sk == SK_MIGHT) return;
    int cost = sk == SK_SMITE ? 2 : sk == SK_BOLT ? 4 + lv / 3 : 6 + lv / 2;
    if (pl.mp < cost) { show_msg("NOT ENOUGH MANA", C_BLUE); return; }
    if (sk == SK_SMITE) {
        if (pl.attacking) return;
        pl.mp -= cost; pl.attacking = 18; pl.atkKind = 1;
        return;
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj* p = &projs[i];
        if (p->active) continue;
        pl.mp -= cost;
        p->active = 1; p->hitmask = 0;
        if (sk == SK_BOLT) {
            p->kind = 0; p->x = pl.x; p->y = pl.y;
            p->vx = dirVX[pl.dir] * 4; p->vy = dirVY[pl.dir] * 4;
            p->life = 45; p->dmg = 10 + lv * 7 + hero.level;
            sfx_bolt();
        } else {
            p->kind = 1; p->cx = pl.x; p->cy = pl.y; p->x = pl.x; p->y = pl.y;
            p->ang = (u8)(pl.dir * 32); p->life = 80; p->dmg = 12 + lv * 8 + hero.level;
            sfx_bolt();
        }
        pl.attacking = 10; pl.atkKind = 2;
        return;
    }
}

static void update_projs(void) {
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj* p = &projs[i];
        if (!p->active) continue;
        if (p->kind == 0) {
            p->x += p->vx; p->y += p->vy;
            if (!walkable(p->x, p->y)) { spark_at(p->x, p->y); p->active = 0; continue; }
        } else {
            int t = 80 - p->life;
            int r = 64 + t * 9;
            p->ang += 7;
            p->x = p->cx + (sin_lut[(u8)(p->ang + 64)] * r >> 8);
            p->y = p->cy + (sin_lut[p->ang] * r >> 8);
        }
        for (int k = 0; k < MAX_MON; k++) {
            Mon* m = &mons[k];
            if (!m->active || m->state == 3 || (p->hitmask & (1u << k))) continue;
            int reach = m->kind == 3 ? 380 : 220;
            if (dist_approx(m->x - p->x, m->y - p->y) < reach) {
                int d = p->dmg * (100 + hero.skill[SK_MIGHT] * 10) / 100;
                if (p->kind == 0 && is_undead(m)) d *= 2;
                hurt_mon(m, d, 0);
                p->hitmask |= 1u << k;
                if (p->kind == 0) { p->active = 0; break; }
            }
        }
        if (p->active && --p->life == 0) p->active = 0;
    }
}

static void player_hurt(int dmg) {
    if (pl.hurt || pl.dead) return;
    dmg = dmg * 100 / (100 + hero.atier * 9 + hero.level * 2);
    if (dmg < 1) dmg = 1;
    pl.hp -= dmg;
    pl.hurt = 15;
    sfx_hit();
    if (pl.hp <= 0) { pl.hp = 0; pl.dead = 1; sfx_die(); }
}

static void update_mons(void) {
    for (int i = 0; i < MAX_MON; i++) {
        Mon* m = &mons[i];
        if (!m->active) continue;
        m->anim++;
        if (m->flash) m->flash--;
        if (m->state == 3) { if (--m->timer == 0) m->active = 0; continue; }
        if (m->stun) { m->stun--; continue; }
        s32 dx = pl.x - m->x, dy = pl.y - m->y;
        int d = dist_approx(dx, dy);
        int sdx = dx - dy;
        if (m->cd) m->cd--;
        if (m->state == 0) {
            if (d < (m->kind == 3 ? 6 * FX : 5 * FX)) m->state = 1;
            continue;
        }
        if (m->state == 2) {
            m->timer--;
            if (m->timer == 10) {
                int reach = m->kind == 3 ? 520 : 330;
                if (dist_approx(dx, dy) < reach) player_hurt(m->dmg + rndr(m->dmg / 2 + 1));
            }
            if (m->timer == 0) { m->state = 1; m->cd = m->kind == 3 ? 36 : 44 + rndr(20); }
            continue;
        }
        // chase
        if (sdx != 0) m->face = sdx < 0;
        int reach = m->kind == 3 ? 460 : 250;
        if (d < reach) {
            if (!m->cd && !pl.dead) { m->state = 2; m->timer = 22; }
            continue;
        }
        if (d > 12 * FX) { m->state = 0; continue; }
        int spd = m->speed;
        s32 vx = 0, vy = 0;
        int ax = iabs(dx), ay = iabs(dy);
        if (ax > ay * 2) vx = dx > 0 ? spd : -spd;
        else if (ay > ax * 2) vy = dy > 0 ? spd : -spd;
        else { vx = (dx > 0 ? spd : -spd) * 7 / 10; vy = (dy > 0 ? spd : -spd) * 7 / 10; }
        // separation from other monsters
        if ((frameCount + i) & 1) {
            for (int k = 0; k < MAX_MON; k++) {
                if (k == i || !mons[k].active || mons[k].state == 3) continue;
                s32 ex = m->x - mons[k].x, ey = m->y - mons[k].y;
                if (iabs(ex) < 160 && iabs(ey) < 160) { vx += ex > 0 ? 4 : -4; vy += ey > 0 ? 4 : -4; }
            }
        }
        move_entity(&m->x, &m->y, vx, vy, m->kind == 3 ? 100 : 70);
    }
}

// ---------------------------------------------------------------- player
static void update_player(void) {
    if (pl.hurt) pl.hurt--;
    if (pl.dead) return;
    // potions
    if ((keysNew & KEY_L) && hero.hpPot) {
        if (pl.hp < pl.maxhp) { hero.hpPot--; pl.hp += pl.maxhp * 45 / 100; if (pl.hp > pl.maxhp) pl.hp = pl.maxhp; sfx_potion(); }
    }
    if ((keysNew & KEY_SELECT) && hero.mpPot) {
        if (pl.mp < pl.maxmp) { hero.mpPot--; pl.mp += pl.maxmp * 60 / 100; if (pl.mp > pl.maxmp) pl.mp = pl.maxmp; sfx_potion(); }
    }
    if (keysNew & KEY_R) {
        for (int k = 1; k <= 3; k++) {
            int s = (pl.cur + k) % 3;
            if (hero.skill[s]) { pl.cur = s; break; }
        }
        static const char* const sn[3] = { "SKILL: SMITE", "SKILL: HOLY BOLT", "SKILL: HOLY HAMMER" };
        show_msg(sn[pl.cur], C_WHITE);
        sfx_menu();
    }
    if (!(frameCount & 31) && pl.mp < pl.maxmp) pl.mp++;
    if (curFloor == 0 && !(frameCount & 7) && pl.hp < pl.maxhp) pl.hp++;

    if (pl.attacking) {
        pl.attacking--;
        if (pl.attacking == 9 && pl.atkKind != 2) {
            if (pl.atkKind == 1) melee_hit(160 + hero.skill[SK_SMITE] * 22, 30);
            else melee_hit(100, 0);
        }
        return;
    }
    // direction from d-pad (8 ways, diagonals included)
    int u = keys & KEY_UP, d = keys & KEY_DOWN, l = keys & KEY_LEFT, r = keys & KEY_RIGHT;
    int dir = -1;
    if (u && r) dir = 3; else if (u && l) dir = 5; else if (d && r) dir = 1; else if (d && l) dir = 7;
    else if (u) dir = 4; else if (d) dir = 0; else if (r) dir = 2; else if (l) dir = 6;
    pl.moving = 0;
    if (dir >= 0) {
        pl.dir = dir;
        move_entity(&pl.x, &pl.y, dirVX[dir], dirVY[dir], 72);
        pl.moving = 1;
        pl.walkt++;
    }
    if (keysNew & KEY_A) { pl.attacking = 16; pl.atkKind = 0; }
    else if (keysNew & KEY_B) cast_skill();
}

// ---------------------------------------------------------------- update / draw
void play_update(void) {
    update_player();
    update_mons();
    update_projs();
    pickup_items();
    for (int i = 0; i < MAX_SPARK; i++) if (sparks[i].t) sparks[i].t--;
    camera_update();
}

void play_draw(void) {
    spr_begin();
    // player
    {
        int pose = 0;
        if (pl.attacking) pose = pl.attacking < 13 ? 3 : 0;
        else if (pl.moving) pose = ((pl.walkt >> 3) & 1) ? 1 : 2;
        int tile = OT_PLAYER + (dirFrame[pl.dir] * 4 + pose) * 16;
        int pal = (pl.hurt && pl.hurt < 16 && (pl.hurt & 4)) ? OP_HURT : OP_PLAYER;
        if (!pl.dead) add32(pl.x, pl.y, tile, pal, dirFlip[pl.dir], 0);
    }
    for (int i = 0; i < MAX_MON; i++) {
        Mon* m = &mons[i];
        if (!m->active) continue;
        if (m->state == 3 && (m->timer & 2)) continue;
        int pose = m->state == 2 ? 2 : ((m->anim >> 3) & 1);
        if (m->state == 0) pose = (m->anim >> 5) & 1;
        int pal = m->flash ? OP_FLASH : (m->kind == 3 ? OP_BOSS : (m->champ ? OP_CHAMP + m->kind : OP_MON + m->kind));
        if (m->kind == 3) {
            int sx, sy; world_to_screen(m->x, m->y, &sx, &sy);
            spr_add(sx - 32, sy - 60, sy, 0x0000, 0xC000, A2(OT_BOSS + pose * 64, pal));
        } else {
            add32(m->x, m->y, OT_MON + (m->kind * 3 + pose) * 16, pal, m->face, 0);
        }
    }
    for (int i = 0; i < MAX_PROJ; i++) {
        Proj* p = &projs[i];
        if (!p->active) continue;
        int t = p->kind == 0 ? SM_BOLT0 : SM_HAM0;
        add16(p->x, p->y, OT_SMALL + (t + ((frameCount >> 2) & 1)) * 4, OP_MISC, 10);
    }
    for (int i = 0; i < MAX_ITEM; i++) {
        Item* it = &items[i];
        if (!it->active) continue;
        static const u8 it2s[5] = { SM_POTR, SM_POTB, SM_GOLD, SM_SWORD, SM_ARMOR };
        int sx, sy; world_to_screen(it->x, it->y, &sx, &sy);
        spr_add(sx - 8, sy - 12, sy - 20, 0x0000, 0x4000, A2(OT_SMALL + it2s[it->kind] * 4, OP_MISC));
    }
    for (int i = 0; i < MAX_SPARK; i++) {
        if (!sparks[i].t) continue;
        spr_add(sparks[i].x - camx - 8, sparks[i].y - camy - 22, 2000,
                0x0000, 0x4000, A2(OT_SMALL + (sparks[i].t > 5 ? SM_SPARK0 : SM_SPARK1) * 4, OP_MISC));
    }
    // portals / town objects
    int pf = (frameCount >> 3) & 1;
    if (curFloor == 0) {
        add32(healerX, healerY, OT_BIG + pf * 16, OP_MISC, 0, 0);
        add16(fireX, fireY, OT_SMALL + (SM_FIRE0 + pf) * 4, OP_MISC, 0);
    }
    add32(portalBlueX, portalBlueY, OT_BIG + 32 + pf * 16, OP_MISC, 0, 0);
    if (portalRedOn || (curFloor == 3 && bossDefeated))
        add32(portalRedX, portalRedY, OT_BIG + 32 + pf * 16, OP_PORTAL_RED, 0, 0);
    spr_end();
}
