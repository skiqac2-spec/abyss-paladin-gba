// Tiny freestanding replacements for routines the compiler may call
#include "gba.h"
typedef unsigned int size_t;
void* memcpy(void* d, const void* s, size_t n) {
    u8* dd = d; const u8* ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void* memset(void* d, int v, size_t n) {
    u8* dd = d;
    while (n--) *dd++ = (u8)v;
    return d;
}
void* memmove(void* d, const void* s, size_t n) {
    u8* dd = d; const u8* ss = s;
    if (dd < ss) while (n--) *dd++ = *ss++;
    else { dd += n; ss += n; while (n--) *--dd = *--ss; }
    return d;
}
