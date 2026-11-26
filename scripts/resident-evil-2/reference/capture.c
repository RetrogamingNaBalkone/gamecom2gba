/* Capture a verified original cartridge using the user's Tigerbyte sources. */
#include <stdint.h>
#include <limits.h>
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

static void capture_write(void *ctx, uint16_t address, uint8_t value) {
    gcbus_write(ctx, address, value);
    if (address == 0x30 || address == 0x31) record_bus("lcd", address, value);
}

int main(int argc, char **argv) {
    if (argc != 7) return 2;
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine, argv[1]) ||
        gcsystem_load_external(&machine, argv[2]) ||
        gcsystem_load_cart(&machine, argv[3])) return 1;
    int compact = getenv("RE2_CAPTURE_CHECKPOINTS") != NULL;
    FILE *audio = compact ? NULL : fopen(argv[4], "wb");
    FILE *trace = fopen(argv[5], "w");
    if ((!compact && !audio) || !trace) return 1;
    const char *restore=getenv("RE2_CAPTURE_NVRAM_IN");
    if (restore) {
        FILE *nv=fopen(restore,"rb");
        if (!nv || fread(machine.bus.ram+0xe000,1,8192,nv)!=8192 || fgetc(nv)!=EOF ||
            fclose(nv)) return 1;
    }
    gcsystem_reset(&machine);
    if (argv[6][0] != '-') {
        dma = fopen(argv[6], "w");
        if (!dma) return 1;
        fprintf(dma, "frame,cycle,pc,event,address,value,mmu1,mmu2,mmu3,mmu4");
        for (int i = 0x30; i <= 0x3f; ++i) fprintf(dma, ",r%02x", i);
        fputc('\n', dma);
        machine.cpu.wr = capture_write;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    int frame = 0, frames, column, row;
    const char *setup = getenv("RE2_CAPTURE_SETUP");
    int setup_frame = setup ? atoi(setup) : -1;
    unsigned in0, in1, in2;
    int state = getenv("RE2_CAPTURE_STATE") != NULL;
    fprintf(trace, "frame,pc,mmu1,mmu2,mmu3,mmu4,lcd,dac_writes,wave_writes,peak,c0,c1,cc,cd,ce");
    if (state) fprintf(trace, ",ram_c0_df,ram_208_240,ram_241_29b");
    fputc('\n', trace);
    int count;
    while ((count = scanf("%d%x%x%x%d%d", &frames, &in0, &in1, &in2, &column, &row)) == 6) {
        int observe = frames > 0;
        if (compact && !observe && frames != INT_MIN) frames = -frames;
        if (frames <= 0 || in0 > 255 || in1 > 255 || in2 > 255 ||
            column < -1 || column >= 13 || row < 0 || row >= 10) return 2;
        gcbus_set_buttons(&machine.bus, in0, in1, in2);
        for (int c = 0; c < 13; ++c)
            gcbus_set_touch(&machine.bus, c, c == column ? 1u << row : 0);
        for (int i = 0; i < frames; ++i) {
            current_frame = frame + 1;
            if (current_frame == setup_frame) {
                const char *at = setup;
                unsigned address, value;
                while ((at = strchr(at, ':')) != NULL) {
                    if (sscanf(++at, "%x=%x", &address, &value) != 2 ||
                        address < 0x80 || address >= 0x1000 || value > 255) return 2;
                    machine.bus.ram[address] = value;
                }
            }
            gcsystem_run_frame(&machine);
            if (machine.cpu.trapped) return 1;
            ++frame;
            if (compact && (!observe || i+1 != frames)) continue;
            int peak = 0, samples = machine.audio_samples * 2;
            for (int j = 0; j < samples; ++j) {
                int amplitude = abs(machine.audio[j]);
                if (amplitude > peak) peak = amplitude;
            }
            if (audio && fwrite(machine.audio, sizeof(int16_t), samples, audio) != (size_t)samples) return 1;
            const uint8_t *r = machine.bus.ram;
            fprintf(trace, "%d,%04x,%02x,%02x,%02x,%02x,%02x,%d,%d,%d,%02x,%02x,%02x,%02x,%02x",
                    frame, machine.cpu.pc, r[0x25], r[0x26], r[0x27], r[0x28],
                    r[0x30], machine.bus.dac_stream_n, machine.bus.wave_stream_n, peak,
                    r[0xc0], r[0xc1], r[0xcc], r[0xcd], r[0xce]);
            if (state) {
                fputc(',', trace);
                for (int j = 0xc0; j <= 0xdf; ++j) fprintf(trace, "%02x", r[j]);
                fputc(',', trace);
                for (int j = 0x208; j <= 0x240; ++j) fprintf(trace, "%02x", r[j]);
                fputc(',', trace);
                for (int j = 0x241; j <= 0x29b; ++j) fprintf(trace, "%02x", r[j]);
            }
            fputc('\n', trace);
        }
        if (!observe) continue;
        uint8_t shades[GC_W * GC_H];
        gc_render_shades(&machine.bus, shades);
        if (fwrite(shades, 1, sizeof shades, stdout) != sizeof shades) return 1;
    }
    const char *persist=getenv("RE2_CAPTURE_NVRAM_OUT");
    if (persist) {
        FILE *nv=fopen(persist,"wb");
        if (!nv || fwrite(machine.bus.ram+0xe000,1,8192,nv)!=8192 || fclose(nv)) return 1;
    }
    if (count != EOF || fclose(trace) || (audio && fclose(audio)) || (dma && fclose(dma)) || fflush(stdout)) return 1;
    return 0;
}
