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
static FILE *dac_file;
static void log_dac(uint8_t v) { fputc(v, dac_file); }
enum { ENTRY_FRAME = 1522 };
static FILE *pclog;
static uint16_t isr_sp_after;
static long isr_start = -1; static int isr_sig; static uint16_t isr_ret; static long isr_counts[64][8]; static int isr_cost[64][8];
static long host_cycle(void) { return (long)host_frame * 82287 + gc_frame_cycle(); }
static uint16_t last_pc; static uint8_t last_d0; static long d0_writers[65536];
void sm8500_profile(uint16_t pc) {
    if (gc_ram[0xd0] != last_d0) { if (gc_ram[0xd0] > last_d0) d0_writers[last_pc]++; last_d0 = gc_ram[0xd0]; }
    last_pc = pc;
    if (getenv("ISRCOST")) {
        if (pc == 0x1076 && isr_start < 0) { isr_start = host_cycle(); isr_sig = 0; isr_ret = 0; isr_sp_after = (uint16_t)(cpu.sp + 3); }
        if (isr_start >= 0) {
            int page = gc_ram[0x26];
            if (page == 0x80 || page == 0x02 || pc < 0x4000) {
                if (pc == 0x5c87 || pc == 0x3586) isr_sig |= 1;
                if (pc == 0x5ce8 || pc == 0x35e7) isr_sig |= 2;
                if (pc == 0x5c9c || pc == 0x359b) isr_sig |= 4;
                if (pc == 0x5cc1 || pc == 0x35c0 || pc == 0x5cf6 || pc == 0x35f6) isr_sig |= 8;
                if (pc == 0x5ca5 || pc == 0x35a4) isr_sig |= 16;
                if (pc == 0x5cdd || pc == 0x5cfb || pc == 0x35dc || pc == 0x35fa) isr_sig |= 32;
                if (pc == 0x357f) isr_sig |= 0;
            }
            if (pc != 0x1076 && cpu.sp == isr_sp_after) {
                long cost = host_cycle() - isr_start;
                int k = 0; while (k < 7 && isr_counts[isr_sig][k] && isr_cost[isr_sig][k] != cost) k++;
                isr_cost[isr_sig][k] = (int)cost; isr_counts[isr_sig][k]++;
                isr_start = -1;
            }
        }
    }
    instructions++;
    if (host_frame + ENTRY_FRAME >= (getenv("HIST_FROM") ? atoi(getenv("HIST_FROM")) : 0)) hist[pc | (gc_ram[0x26] == 0x80 ? 0 : 0)]++;
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
    indy_launch(argc > 5 ? (!strcmp(argv[5], "native") ? 3 : atoi(argv[5])) : 0);
    if (getenv("DMALOG")) gc_dma_hook = log_dma;
    if (getenv("DACLOG")) { dac_file = fopen(getenv("DACLOG"), "wb"); gc_dac_hook = log_dac; }
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
    if (dac_file) fclose(dac_file);
    fprintf(stderr, "frames %d pc %04x instructions %lu blits %lu vram %lu\n", done, cpu.pc, instructions, blits, vram_writes);
    for (int a = 0; a < 65536; ++a) if (d0_writers[a] > 100) fprintf(stderr, "d0 increased after %04x: %ld\n", a, d0_writers[a]);
    if (getenv("ISRCOST"))
        for (int g = 0; g < 64; ++g) for (int k = 0; k < 8; ++k)
            if (isr_counts[g][k]) fprintf(stderr, "isr sig %02x cost %d count %ld\n", g, isr_cost[g][k], isr_counts[g][k]);
    if (getenv("COUNTPC")) {
        char *list = strdup(getenv("COUNTPC"));
        for (char *t = strtok(list, ","); t; t = strtok(0, ","))
            fprintf(stderr, "pc %s: %lu\n", t, hist[strtoul(t, 0, 16)]);
    }
    if (getenv("HIST")) {
        for (int k = 0; k < (getenv("HIST_N") ? atoi(getenv("HIST_N")) : 30); ++k) {
            unsigned best = 0;
            for (unsigned a = 1; a < 65536; ++a) if (hist[a] > hist[best]) best = a;
            fprintf(stderr, "  %04x %lu\n", best, hist[best]);
            hist[best] = 0;
        }
    }
    return 0;
}
