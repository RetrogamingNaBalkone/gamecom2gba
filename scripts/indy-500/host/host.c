/* Host build of the shared game.com runtime for lockstep checks against Tigerbyte.
 *
 *   host CART BIOS FRAMES OUT [native]
 *
 * stdin holds input rows "frames in0 in1 in2" (hexadecimal ports, active low).
 * OUT receives RAM 0000-03FF after every display period. The first period is the
 * remainder of the one in which the BIOS started the cartridge. Pass "native" to
 * enable the PCM fast path and idle hints, which change the cycle timing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../gba/gamecom.h"
#include "../../../gba/sm8500.h"
#include "../../../gba/indy-500/launch.h"

unsigned long blits, vram_writes, instructions;
static unsigned long hist[65536];
extern int gc_frame_cycle(void);
static long pclog_left = -1;
extern int host_frame;
enum { ENTRY_FRAME = 1522 };
static FILE *pclog;
void sm8500_profile(uint16_t pc) {
    instructions++; hist[pc]++;
    if (pclog_left < 0) {
        pclog_left = getenv("PCLOG") ? 200000 : 0;
        if (pclog_left) pclog = fopen(getenv("PCLOG"), "w");
    }
    if (pclog_left > 0 && host_frame + ENTRY_FRAME >= atoi(getenv("PCLOG_FROM") ? getenv("PCLOG_FROM") : "0")) {
        fprintf(pclog, "%04x %d %02x %02x\n", pc, gc_frame_cycle() + 0, gc_ram[0x26], gc_ram[0x27]);
        if (--pclog_left == 0) fclose(pclog);
    }
}
int host_frame;
extern void (*gc_dma_hook)(int, uint8_t);
static void log_dma(int cycle, uint8_t dmc) {
    if (host_frame < atoi(getenv("DMALOG"))) fprintf(stderr, "dma %d %d %02x\n", host_frame, cycle, dmc);
}
extern int gc_frame_cycle(void);
void video_blit(const gc_blit_t *b, const uint8_t *s, uint16_t m, int w) {
    (void)s; (void)m; (void)w; blits++;
    if (getenv("BLITLOG") && host_frame < atoi(getenv("BLITLOG")))
        fprintf(stderr, "blit frame %d cycle %d pc %04x dmc %02x %dx%d\n", host_frame, gc_frame_cycle(), cpu.pc, b->dmc, b->w + 1, b->h + 1);
}
void video_vram_write(uint16_t o, uint8_t v) { (void)o; (void)v; vram_writes++; }
void video_vram_copy(const gc_blit_t *b) { (void)b; }

static uint8_t *load(const char *path, long size) {
    FILE *f = fopen(path, "rb");
    uint8_t *d = malloc(size);
    if (!f || fread(d, 1, size, f) != (size_t)size) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    fclose(f);
    return d;
}

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    gc_media_t media = { load(argv[1], 0x100000), 0xfffff, load(argv[2], 0x40000) };
    gc_init(&media);
    indy_launch(argc > 5 && !strcmp(argv[5], "native"));
    if (getenv("DMALOG")) gc_dma_hook = log_dma;
    FILE *out = fopen(argv[4], "wb");
    if (!out) return 1;
    int frames = atoi(argv[3]), done = 0, n = 0;
    unsigned in0 = 0xff, in1 = 0xff, in2 = 0xff;
    while (done < frames) {
        if (n == 0 && scanf("%d %x %x %x", &n, &in0, &in1, &in2) != 4) { n = frames; in0 = in1 = in2 = 0xff; }
        gc_set_input((uint8_t)in0, (uint8_t)in1, (uint8_t)in2);
        gc_run_frame();
        host_frame++;
        --n;
        ++done;
        unsigned char ram[0x400];
        for (int a = 0; a < 0x400; ++a) ram[a] = gc_read((uint16_t)(a < 0x10 ? 0x10 : a));
        memcpy(ram + 0x10, gc_ram + 0x10, 0x3f0);
        ram[0x19] = cpu.sys; ram[0x1c] = (uint8_t)(cpu.sp >> 8); ram[0x1d] = (uint8_t)cpu.sp;
        ram[0x1e] = cpu.ps0; ram[0x1f] = cpu.ps1;
        memset(ram, 0, 0x10);
        fwrite(ram, 1, sizeof ram, out);
        if (cpu.trapped) { fprintf(stderr, "trap at %04x after %d frames\n", cpu.trap_pc, done); break; }
    }
    fclose(out);
    fprintf(stderr, "frames %d pc %04x instructions %lu blits %lu vram %lu\n", done, cpu.pc, instructions, blits, vram_writes);
    if (getenv("HIST")) {
        for (int k = 0; k < 30; ++k) {
            unsigned best = 0;
            for (unsigned a = 1; a < 65536; ++a) if (hist[a] > hist[best]) best = a;
            fprintf(stderr, "  %04x %lu\n", best, hist[best]);
            hist[best] = 0;
        }
    }
    return 0;
}
