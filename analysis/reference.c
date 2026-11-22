/* Headless reference capture. Link with the globally installed Tigerbyte sources.
 * Input rows on stdin: frames, active-low IN0, IN1, IN2 (hex), touch column/row.
 * Output: final LCD as PGM on stdout, machine state on stderr.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "sys/gcsystem.h"
#include "sys/ppu.h"

static gcsystem_t machine;
static int frame_number;

static uint8_t trace_read(void *context, uint16_t address) {
    uint8_t value = gcbus_read(context, address);
    if (address >= 0x8000 && address < 0xa000)
        fprintf(stderr, "%d read pc=%04x bank=%02x addr=%04x value=%02x\n",
            frame_number, machine.cpu.pc, machine.bus.ram[0x28], address, value);
    return value;
}

static void trace_write(void *context, uint16_t address, uint8_t value) {
    const uint8_t *r = machine.bus.ram;
    if (address == 0x34 && (value & 0x80)) {
        fprintf(stderr, "%d blit %02x bank=%02x src=%d,%d size=%d,%d dst=%d,%d pal=%02x\n",
            frame_number, value, r[0x3c], r[0x36], r[0x35], r[0x38]+1, r[0x37]+1,
            r[0x3a], r[0x39], r[0x3b]);
    }
    gcbus_write(context, address, value);
}

int main(int argc, char **argv) {
    if (argc != 4 || !(argv[1][0] && argv[2][0] && argv[3][0])) return 2;
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine, argv[1]) ||
        gcsystem_load_external(&machine, argv[2]) ||
        gcsystem_load_cart(&machine, argv[3])) return 1;
    gcsystem_reset(&machine);
    const char *trace = getenv("GAMECOM_TRACE_FRAME");
    int frames, col, row, read;
    unsigned in0, in1, in2;
    while ((read = scanf("%d%x%x%x%d%d", &frames, &in0, &in1, &in2, &col, &row)) == 6) {
        if (frames < 0 || in0 > 255 || in1 > 255 || in2 > 255 ||
            col < -1 || col > 12 || row < 0 || row > 9) return 2;
        gcbus_set_buttons(&machine.bus, in0, in1, in2);
        for (int c = 0; c < 13; c++)
            gcbus_set_touch(&machine.bus, c, c == col ? 1u << row : 0);
        for (int f = 0; f < frames; f++) {
            if (trace && frame_number == atoi(trace)) machine.cpu.wr = trace_write;
            if (trace && frame_number == atoi(trace) && getenv("GAMECOM_TRACE_READS"))
                machine.cpu.rd = trace_read;
            gcsystem_run_frame(&machine);
            frame_number++;
            if (machine.cpu.trapped) return 1;
        }
    }
    if (read != EOF) return 2;
    if (getenv("GAMECOM_TRACE_STATE")) {
        for (int offset = 0xb0; offset < 0x100; offset += 16) {
            fprintf(stderr, "%02x:", offset);
            for (int i = 0; i < 16; i++) fprintf(stderr, " %02x", machine.bus.ram[offset+i]);
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "PC=%04x MMU=%02x.%02x.%02x.%02x LCD=%02x\n", machine.cpu.pc,
        machine.bus.ram[0x25], machine.bus.ram[0x26], machine.bus.ram[0x27],
        machine.bus.ram[0x28], machine.bus.ram[0x30]);
    uint8_t pixels[GC_W * GC_H];
    gc_render_shades(&machine.bus, pixels);
    for (unsigned i = 0; i < sizeof pixels; i++) pixels[i] = pixels[i] * 63;
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    printf("P5\n%d %d\n255\n", GC_W, GC_H);
    return fwrite(pixels, 1, sizeof pixels, stdout) == sizeof pixels ? 0 : 1;
}
