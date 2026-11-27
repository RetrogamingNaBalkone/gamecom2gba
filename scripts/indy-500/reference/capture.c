/* Replay an input tape on the verified Indy 500 cartridge using the user's Tigerbyte sources.
   stdin rows: frames in0 in1 in2 column row  (frames<0: advance without a snapshot)
   For every snapshot, stdout receives 32000 LCD shade bytes, 4096 bytes of RAM 0000-0FFF
   and 8192 bytes of RAM E000-FFFF. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "sys/gcsystem.h"
#include "sys/ppu.h"

static gcsystem_t machine;
#ifdef INDY_COVERAGE
static uint8_t executed[0x100000];
static int dumped;
static void dump_entry_state(uint16_t pc) {
    const char *path = getenv("INDY_ENTRY_STATE");
    if (dumped || !path || pc != 0x4020 || machine.bus.ram[0x26] != 0x80) return;
    dumped = 1;
    FILE *f = fopen(path, "wb");
    if (!f) return;
    /* 4096 bytes RAM/IO, 8192 bytes E000-FFFF, 256 register-file bytes, then pc, sp, ps0, ps1, r2 */
    fwrite(machine.bus.ram, 1, 4096, f);
    fwrite(machine.bus.ram + 0xe000, 1, 8192, f);
    fwrite(machine.cpu.reg, 1, 256, f);
    uint8_t tail[8] = { pc >> 8, pc & 0xff, machine.cpu.sp >> 8, machine.cpu.sp & 0xff,
                        machine.cpu.ps0, machine.cpu.ps1, machine.cpu.reg[(machine.cpu.ps0 & 0xf8) + 2], 0 };
    fwrite(tail, 1, 8, f);
    fclose(f);
}
void capture_pc(uint16_t pc) {
    dump_entry_state(pc);
    if (pc < 0x2000 || pc >= 0xa000) return;
    uint8_t page = machine.bus.ram[0x25 + ((pc - 0x2000) >> 13)];
    if (page < 0x20) return;
    executed[(((uint32_t)page << 13) | (pc & 0x1fff)) & 0xfffff] = 1;
}
#endif
static FILE *dma;
static int current_frame;

static void record_bus(const char *event, uint16_t address, uint8_t value) {
    const uint8_t *r = machine.bus.ram;
    fprintf(dma, "%d,%d,%04x,%s,%04x,%02x", current_frame, machine.bus.cur_cycle,
            machine.cpu.pc, event, address, value);
    for (int i = 0x25; i <= 0x28; ++i) fprintf(dma, ",%02x", r[i]);
    for (int i = 0x30; i <= 0x3f; ++i) fprintf(dma, ",%02x", r[i]);
    fputc('\n', dma);
}

void capture_dma_begin(void) {
    if (dma) record_bus("dma", 0x34, machine.bus.ram[0x34]);
}

static FILE *watchlog;
static unsigned watch0, watch1;
static void capture_write(void *ctx, uint16_t address, uint8_t value) {
    gcbus_write(ctx, address, value);
    if (dma && (address == 0x30 || address == 0x31)) record_bus("lcd", address, value);
    if (watchlog && address >= watch0 && address <= watch1) {
        const uint8_t *r = machine.bus.ram;
        fprintf(watchlog, "%d,%04x,%02x,%02x,%04x,%02x\n", current_frame, machine.cpu.pc,
                r[0x26], r[0x27], address, value);
    }
}

int main(int argc, char **argv) {
    if (argc != 7) return 2;
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine, argv[1]) ||
        gcsystem_load_external(&machine, argv[2]) ||
        gcsystem_load_cart(&machine, argv[3])) return 1;
    FILE *trace = argv[4][0] != '-' ? fopen(argv[4], "w") : NULL;
    FILE *audio = argv[6][0] != '-' ? fopen(argv[6], "wb") : NULL;
    gcsystem_reset(&machine);
    const char *wspec = getenv("INDY_WATCH");   /* start-end:path */
    if (wspec) {
        char path[1024];
        if (sscanf(wspec, "%x-%x:%1023s", &watch0, &watch1, path) != 3) return 2;
        watchlog = fopen(path, "w");
        if (!watchlog) return 1;
        fprintf(watchlog, "frame,pc,mmu2,mmu3,address,value\n");
        machine.cpu.wr = capture_write;
    }
    if (argv[5][0] != '-') {
        dma = fopen(argv[5], "w");
        if (!dma) return 1;
        fprintf(dma, "frame,cycle,pc,event,address,value,mmu1,mmu2,mmu3,mmu4");
        for (int i = 0x30; i <= 0x3f; ++i) fprintf(dma, ",r%02x", i);
        fputc('\n', dma);
        machine.cpu.wr = capture_write;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const char *watch = getenv("INDY_TRACE");   /* hex start-end of RAM traced every frame */
    unsigned w0 = 0, w1 = 0;
    if (watch && sscanf(watch, "%x-%x", &w0, &w1) != 2) return 2;
    int frame = 0, frames, column, row;
    unsigned in0, in1, in2;
    if (trace) fprintf(trace, "frame,pc,mmu1,mmu2,mmu3,mmu4,dac,ram\n");
    int count;
    while ((count = scanf("%d%x%x%x%d%d", &frames, &in0, &in1, &in2, &column, &row)) == 6) {
        int observe = frames > 0;
        if (!observe) frames = -frames;
        if (frames <= 0 || column < -1 || column >= 13 || row < 0 || row >= 10) return 2;
        gcbus_set_buttons(&machine.bus, in0, in1, in2);
        for (int c = 0; c < 13; ++c)
            gcbus_set_touch(&machine.bus, c, c == column ? 1u << row : 0);
        for (int i = 0; i < frames; ++i) {
            current_frame = frame + 1;
            gcsystem_run_frame(&machine);
            if (machine.cpu.trapped) { fprintf(stderr, "CPU trapped at frame %d pc %04x\n", frame, machine.cpu.pc); return 1; }
            ++frame;
            if (audio) fwrite(machine.audio, sizeof(int16_t), machine.audio_samples * 2, audio);
            if (trace) {
                const uint8_t *r = machine.bus.ram;
                fprintf(trace, "%d,%04x,%02x,%02x,%02x,%02x,%d,", frame, machine.cpu.pc,
                        r[0x25], r[0x26], r[0x27], r[0x28], machine.bus.dac_stream_n);
                for (unsigned a = w0; a <= w1 && w1; ++a) fprintf(trace, "%02x", r[a]);
                fputc('\n', trace);
            }
        }
        if (!observe) continue;
        uint8_t shades[GC_W * GC_H];
        gc_render_shades(&machine.bus, shades);
        fwrite(shades, 1, sizeof shades, stdout);
        fwrite(machine.bus.ram, 1, 4096, stdout);
        fwrite(machine.bus.ram + 0xe000, 1, 8192, stdout);
    }
    if (count != EOF) return 2;
#ifdef INDY_COVERAGE
    FILE *cov = fopen(getenv("INDY_COVERAGE"), "wb");
    if (!cov || fwrite(executed, 1, sizeof executed, cov) != sizeof executed) return 1;
    fclose(cov);
#endif
    if (trace) fclose(trace);
    if (audio) fclose(audio);
    if (dma) fclose(dma);
    if (watchlog) fclose(watchlog);
    fflush(stdout);
    return 0;
}
