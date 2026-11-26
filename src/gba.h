// Minimal GBA hardware definitions (no external libraries)
#ifndef GBA_H
#define GBA_H

typedef unsigned char u8;   typedef signed char s8;
typedef unsigned short u16; typedef signed short s16;
typedef unsigned int u32;   typedef signed int s32;
typedef unsigned long long u64;

#define REG16(a) (*(volatile u16*)(a))
#define REG32(a) (*(volatile u32*)(a))

#define REG_DISPCNT   REG16(0x04000000)
#define REG_DISPSTAT  REG16(0x04000004)
#define REG_VCOUNT    REG16(0x04000006)
#define REG_BGCNT(n)  REG16(0x04000008 + 2 * (n))
#define REG_BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define REG_BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define REG_WIN0H     REG16(0x04000040)
#define REG_WIN0V     REG16(0x04000044)
#define REG_WININ     REG16(0x04000048)
#define REG_WINOUT    REG16(0x0400004A)
#define REG_BLDCNT    REG16(0x04000050)
#define REG_BLDALPHA  REG16(0x04000052)
#define REG_BLDY      REG16(0x04000054)

#define REG_SND1SWEEP REG16(0x04000060)
#define REG_SND1CNT   REG16(0x04000062)
#define REG_SND1FREQ  REG16(0x04000064)
#define REG_SND2CNT   REG16(0x04000068)
#define REG_SND2FREQ  REG16(0x0400006C)
#define REG_SND4CNT   REG16(0x04000078)
#define REG_SND4FREQ  REG16(0x0400007C)
#define REG_SNDDMGCNT REG16(0x04000080)
#define REG_SNDDSCNT  REG16(0x04000082)
#define REG_SNDSTAT   REG16(0x04000084)

#define REG_DMA0SAD   REG32(0x040000B0)
#define REG_DMA0DAD   REG32(0x040000B4)
#define REG_DMA0CNT_L REG16(0x040000B8)
#define REG_DMA0CNT_H REG16(0x040000BA)
#define REG_DMA3SAD   REG32(0x040000D4)
#define REG_DMA3DAD   REG32(0x040000D8)
#define REG_DMA3CNT   REG32(0x040000DC)
#define REG_KEYINPUT  REG16(0x04000130)
#define REG_WAITCNT   REG16(0x04000204)

#define PAL_BG   ((volatile u16*)0x05000000)
#define PAL_OBJ  ((volatile u16*)0x05000200)
#define VRAM     ((volatile u16*)0x06000000)
#define OAM      ((volatile u16*)0x07000000)
#define SRAM     ((volatile u8*)0x0E000000)

#define CHARBLOCK(n)  ((volatile u32*)(0x06000000 + (n) * 0x4000))
#define SCREENBLOCK(n) ((volatile u16*)(0x06000000 + (n) * 0x800))
#define OBJ_TILES     ((volatile u32*)0x06010000)

#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200

#define IWRAM_CODE __attribute__((section(".iwram"), long_call, target("arm")))
#define EWRAM_BSS  __attribute__((section(".ewram_bss")))

static inline void dma3_copy32(volatile void* dst, const void* src, u32 words) {
    REG_DMA3SAD = (u32)src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = words | 0x84000000;
}
static inline void dma3_fill32(volatile void* dst, u32 val, u32 words) {
    static volatile u32 v;
    v = val;
    REG_DMA3SAD = (u32)&v;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = words | 0x85000000;
}

#endif
