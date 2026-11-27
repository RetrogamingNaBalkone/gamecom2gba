/* Host build of the shared game.com runtime core for lockstep checks against Tigerbyte.
   usage: host cart.tgc bios.bin frames out.ram [tape rows on stdin: frames in0 in1 in2] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../gba/gamecom.h"
#include "../../../gba/sm8500.h"
#include "../../../gba/pcm_isr.h"
#include "../../../gba/indy-500/entry_state.h"

unsigned long blits, vram_writes, instructions;
static unsigned long hist[65536];
void sm8500_profile(uint16_t pc) { instructions++; hist[pc]++; }
void video_blit(const gc_blit_t *b, const uint8_t *s, uint16_t m, int w) { (void)b; (void)s; (void)m; (void)w; blits++; }
void video_vram_write(uint16_t o, uint8_t v) { (void)o; (void)v; vram_writes++; }
void video_vram_copy(const gc_blit_t *b) { (void)b; }

static uint8_t *load(const char *path, long size) {
    FILE *f = fopen(path, "rb");
    uint8_t *d = malloc(size);
    if (!f || fread(d, 1, size, f) != (size_t)size) { fprintf(stderr, "cannot read %s\n", path); exit(1); }
    fclose(f);
    return d;
}

static const pcm_isr_t indy_pcm[] = {
    { 0x357f, 0, 0x36d4, 0x35dc, 0x35fa, 1, 0 },
    { 0x5c7e, 0x80, 0x36d4, 0x5cdd, 0x5cfb, 0, 1 },
    { 0 } };

int main(int argc, char **argv) {
    if (argc < 5) return 2;
    gc_media_t media = { load(argv[1], 0x100000), 0xfffff, load(argv[2], 0x40000) };
    gc_init(&media);
    for (unsigned i = 0; i < sizeof entry_ram / sizeof entry_ram[0]; ++i) {
        uint16_t a = entry_ram[i].address;
        if (a >= 0xe000) gc_ext[a - 0xe000] = entry_ram[i].value; else gc_ram[a] = entry_ram[i].value;
    }
    sm8500_reset_state();
    for (unsigned i = 0; i < sizeof entry_registers / sizeof entry_registers[0]; ++i)
        cpu.reg[entry_registers[i].address] = entry_registers[i].value;
    cpu.pc = ENTRY_PC; cpu.sp = ENTRY_SP; cpu.ps0 = ENTRY_PS0; cpu.ps1 = ENTRY_PS1; cpu.sys = ENTRY_SYS;
    for (uint16_t a = 0x25; a <= 0x28; ++a) gc_write(a, gc_ram[a]);
    if (!getenv("NO_PCM_HLE")) gc_pcm_isr = indy_pcm;
    int frames = atoi(argv[3]), done = 0, n;
    unsigned in0, in1, in2;
    while (done < frames) {
        if (scanf("%d %x %x %x", &n, &in0, &in1, &in2) != 4) { n = frames - done; in0 = in1 = in2 = 0xff; }
        gc_set_input(in0, in1, in2);
        for (int i = 0; i < n && done < frames; ++i, ++done) {
            gc_run_frame();
            if (cpu.trapped) { fprintf(stderr, "trap at %04x frame %d\n", cpu.trap_pc, done); return 1; }
            if (getenv("VERBOSE") && done % 60 == 0) {
                fprintf(stderr, "frame %d pc %04x instr/60f %lu blits %lu vram %lu lcdc %02x d5 %02x\n", done,
                        cpu.pc, instructions, blits, vram_writes, gc_ram[0x30], gc_ram[0xd5]);
                instructions = 0;
            }
        }
    }
    FILE *o = fopen(argv[4], "wb");
    fwrite(gc_ram, 1, 4096, o); fwrite(gc_ext, 1, 8192, o); fclose(o);
    fprintf(stderr, "pc %04x blits %lu vram %lu\n", cpu.pc, blits, vram_writes);
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
