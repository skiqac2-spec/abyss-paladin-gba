// Headless mGBA test harness: runs a ROM with a scripted input sequence,
// writes screenshots (PPM) and reads memory for automated checks.
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba-util/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <mgba/core/log.h>
static void nolog(struct mLogger* l, int c, enum mLogLevel lv, const char* f, va_list a) { (void)l; (void)c; (void)lv; (void)f; (void)a; }
static struct mLogger silent = { .log = nolog };

static void shot(color_t* buf, unsigned w, unsigned h, const char* name) {
    FILE* f = fopen(name, "wb");
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; i++) {
        uint32_t c = buf[i];
        unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: gbatest rom.gba save.sav script.txt\n"); return 1; }
    mLogSetDefaultLogger(&silent);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) { fprintf(stderr, "core init failed\n"); return 1; }
    mCoreInitConfig(core, NULL);
    unsigned w, h; core->desiredVideoDimensions(core, &w, &h);
    color_t* buf = calloc(w * h, sizeof(color_t));
    core->setVideoBuffer(core, buf, w);
    if (!mCoreLoadFile(core, argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
    struct VFile* sv = VFileOpen(argv[2], O_CREAT | O_RDWR);
    if (sv) core->loadSave(core, sv);
    core->reset(core);
    FILE* sc = fopen(argv[3], "r");
    char line[256];
    long frame = 0;
    while (fgets(line, sizeof line, sc)) {
        char cmd[32]; unsigned a = 0, b = 0; char name[200];
        if (sscanf(line, "%31s", cmd) != 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "KEYS")) {
            sscanf(line, "%*s %x %u", &a, &b);
            core->setKeys(core, a);
            for (unsigned i = 0; i < b; i++) { core->runFrame(core); frame++; }
        } else if (!strcmp(cmd, "SHOT")) {
            sscanf(line, "%*s %199s", name);
            shot(buf, w, h, name);
        } else if (!strcmp(cmd, "PEEK")) {
            sscanf(line, "%*s %x %199s", &a, name);
            printf("%s=%d\n", name, (int32_t)core->busRead32(core, a));
        } else if (!strcmp(cmd, "POKE")) {
            int v = 0;
            sscanf(line, "%*s %x %d", &a, &v);
            core->busWrite32(core, a, (uint32_t)v);
        } else if (!strcmp(cmd, "COPYADD")) {
            unsigned src = 0; int d = 0;
            sscanf(line, "%*s %x %x %d", &a, &src, &d);
            core->busWrite32(core, a, core->busRead32(core, src) + d);
        } else if (!strcmp(cmd, "POKE8")) {
            int v = 0;
            sscanf(line, "%*s %x %d", &a, &v);
            core->busWrite8(core, a, (uint8_t)v);
        } else if (!strcmp(cmd, "PEEK8")) {
            sscanf(line, "%*s %x %199s", &a, name);
            printf("%s=%d\n", name, (int)core->busRead8(core, a));
        } else if (!strcmp(cmd, "PEEK16")) {
            sscanf(line, "%*s %x %199s", &a, name);
            printf("%s=%d\n", name, (int16_t)core->busRead16(core, a));
        }
    }
    fflush(stdout);
    core->deinit(core);
    return 0;
}
