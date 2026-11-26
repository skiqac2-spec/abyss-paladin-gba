// ABYSS PALADIN - shared game definitions
#ifndef GAME_H
#define GAME_H
#include "gba.h"
#include "assets.h"

#ifndef ACT
#define ACT 1
#endif

#define MAPW 48
#define MAPH 48
#define T_VOID 0
#define T_FLOOR 1
#define T_FLOOR2 2
#define T_WALL 3

#define FX 256              // world fixed point: 1 tile = 256
#define MAX_MON 28
#define MAX_PROJ 12
#define MAX_ITEM 24
#define MAX_SPARK 8

// OBJ tile bases
#define OT_PLAYER 0
#define OT_MON    320
#define OT_BOSS   464
#define OT_BIG    656
#define OT_SMALL  720
// small misc indices
enum { SM_FIRE0, SM_FIRE1, SM_POTR, SM_POTB, SM_GOLD, SM_SWORD, SM_ARMOR, SM_BOLT0, SM_BOLT1, SM_HAM0, SM_HAM1, SM_SPARK0, SM_SPARK1 };

// OBJ palette banks
#define OP_PLAYER 0
#define OP_MON    1     // 1..3
#define OP_BOSS   4
#define OP_CHAMP  5     // 5..7
#define OP_MISC   8
#define OP_PORTAL_RED 9
#define OP_FLASH  10
#define OP_HURT   11

// BG palette banks
#define BP_MAP 0
#define BP_FONT 1        // 1..8 text colors
#define BP_HUD 9
#define BP_TITLE 10
enum { C_WHITE, C_RED, C_GOLD, C_BLUE, C_GRAY, C_YELLOW, C_GREEN, C_BRIGHT };

// skills
enum { SK_SMITE, SK_BOLT, SK_HAMMER, SK_MIGHT, SK_COUNT };

typedef struct {
    u8 diff;        // 0 normal 1 nightmare 2 hell
    u8 act;         // 1..5 (act this character is in)
    u8 level;
    u8 wtier, atier;
    u8 skill[SK_COUNT];
    u8 hpPot, mpPot;
    u8 deepest;     // deepest dungeon floor reached in this act (0..3)
    u32 xp;
    u32 gold;
} Hero;

typedef struct {
    s32 x, y;
    s16 hp, maxhp, mp, maxmp;
    u8 dir;         // screen dir 0..7 (0=S, then counter-clockwise: SE,E,NE,N,NW,W,SW)
    u8 attacking;   // frames left in attack
    u8 atkKind;     // 0 normal, 1 smite
    u8 hurt;        // invulnerability frames
    u8 cur;         // selected active skill
    u16 walkt;
    u8 moving;
    u8 dead;
} Player;

typedef struct {
    s32 x, y;
    s16 hp, maxhp;
    s16 dmg;
    u16 xp;
    u8 active, kind;    // kind 0..2 normal, 3 boss
    u8 state, timer;    // state 0 idle 1 chase 2 attack 3 dying
    u8 champ, face;
    u8 anim, cd, stun, flash, speed, hitby;
} Mon;

typedef struct {
    s32 x, y, vx, vy, cx, cy;
    s16 dmg;
    u8 active, kind, life, ang;
    u32 hitmask;
} Proj;

typedef struct {
    s32 x, y;
    u8 active, kind, quality, tier;
    u16 value;
} Item;

extern Hero hero;
extern Player pl;
extern Mon mons[MAX_MON];
extern Proj projs[MAX_PROJ];
extern Item items[MAX_ITEM];
extern u8 map[MAPW * MAPH];
extern int curFloor;
extern int camx, camy;
extern u32 frameCount;
extern u16 keys, keysNew, keysOld;
extern int bossAlive, bossDefeated;
extern s32 portalBlueX, portalBlueY, portalRedX, portalRedY;
extern int portalRedOn;
extern s32 healerX, healerY, fireX, fireY;

// main.c
u32 rnd(void);
int rndr(int n);
void vsync(void);
void read_keys(void);
void text_clear(void);
void text_put(int x, int y, const char* s, int col);
void text_num(int x, int y, int v, int width, int col);
void text_center(int y, const char* s, int col);
void show_msg(const char* s, int col);
void hud_draw(void);
int itoa_buf(char* b, int v);
void save_write(void);
int player_damage_roll(void);
int hero_maxhp(void);
int hero_maxmp(void);
u32 xp_need(int lv);
const char* weapon_name(int t);
const char* armor_name(int t);
void on_act_clear(void);
extern const char* const actNames[5];
extern const char* const floorNames[5][4];

// world.c
void world_load_gfx(void);
void gen_floor(int floor);
int tile_at(int i, int j);
int walkable(s32 x, s32 y);
void map_redraw_full(void);
void map_scroll_update(void);
void camera_update(void);
void light_update(int on);
void light_init(void);
void world_to_screen(s32 x, s32 y, int* sx, int* sy);

// play.c
void play_init_floor(int floor, int fromTown);
void play_update(void);
void play_draw(void);
void spr_begin(void);
void spr_add(int x, int y, int depth, u16 a0, u16 a1, u16 a2);
void spr_end(void);
void oam_flush(void);
void player_recalc(void);
void give_xp(u32 xp);

// sound.c
void snd_init(void);
void snd_update(void);
void sfx_swing(void);
void sfx_hit(void);
void sfx_bolt(void);
void sfx_potion(void);
void sfx_levelup(void);
void sfx_pickup(void);
void sfx_menu(void);
void sfx_die(void);
void sfx_portal(void);
void sfx_boss(void);

#endif
