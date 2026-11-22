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

static void combat_write(void *context, uint16_t address, uint8_t value) {
    const uint8_t *r = machine.bus.ram;
    if (address==0x208 && r[0x27]==0x22 && machine.cpu.pc==0x6c04) {
        unsigned actor=(unsigned)r[0xe4]*256+r[0xe5];
        if (actor>=0x224 && actor<0x2fc)
            fprintf(stderr,"attack frame=%d actor=%04x type=%u x=%u depth=%u health=%u armor=%u\n",
                frame_number,actor,r[actor+3]>>5,r[actor+10]*256+r[actor+11],r[actor+2],
                100*r[0x20a]+10*r[0x209]+value,
                100*r[0x21e]+10*r[0x21d]+r[0x21c]);
    }
    gcbus_write(context,address,value);
}

static void trace_write(void *context, uint16_t address, uint8_t value) {
    const uint8_t *r = machine.bus.ram;
    if (machine.cpu.pc==0x70c2 && r[0x27]==0x22 && getenv("GAMECOM_TRACE_ACTORS"))
        fprintf(stderr,"spawn %d %04x room=%02x lane=%d depth=%d rng=%02x%02x last=%02x\n",
                frame_number,address,value,r[0xe2],r[0xe1],r[0xd2],r[0xd3],r[0xd1]);
    if (address == 0x34 && (value & 0x80)) {
        fprintf(stderr, "%d blit %02x bank=%02x src=%d,%d size=%d,%d dst=%d,%d pal=%02x\n",
            frame_number, value, r[0x3c], r[0x36], r[0x35], r[0x38]+1, r[0x37]+1,
            r[0x3a], r[0x39], r[0x3b]);
    }
    combat_write(context, address, value);
}

int main(int argc, char **argv) {
    if (argc != 4 || !(argv[1][0] && argv[2][0] && argv[3][0])) return 2;
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine, argv[1]) ||
        gcsystem_load_external(&machine, argv[2]) ||
        gcsystem_load_cart(&machine, argv[3])) return 1;
    gcsystem_reset(&machine);
    const char *trace = getenv("GAMECOM_TRACE_FRAME");
    const char *walk = getenv("GAMECOM_TRACE_WALK");
    int walk_start = walk ? atoi(walk) : -1;
    const char *combat = getenv("GAMECOM_TRACE_COMBAT");
    int combat_start = combat ? atoi(combat) : -1;
    const char *motion = getenv("GAMECOM_TRACE_ACTOR_MOTION");
    int motion_start = motion ? atoi(motion) : -1;
    int frames, col, row, read;
    unsigned in0, in1, in2;
    while ((read = scanf("%d%x%x%x%d%d", &frames, &in0, &in1, &in2, &col, &row)) == 6) {
        if (frames < 0 || in0 > 255 || in1 > 255 || in2 > 255 ||
            col < -1 || col > 12 || row < 0 || row > 9) return 2;
        gcbus_set_buttons(&machine.bus, in0, in1, in2);
        for (int c = 0; c < 13; c++)
            gcbus_set_touch(&machine.bus, c, c == col ? 1u << row : 0);
        for (int f = 0; f < frames; f++) {
            if (frame_number == combat_start && machine.cpu.wr != trace_write)
                machine.cpu.wr = combat_write;
            if (trace && frame_number == atoi(trace)) machine.cpu.wr = trace_write;
            if (trace && frame_number == atoi(trace) && getenv("GAMECOM_TRACE_READS"))
                machine.cpu.rd = trace_read;
            unsigned old_lane = machine.bus.ram[0xc4], old_depth = machine.bus.ram[0xc6];
            gcsystem_run_frame(&machine);
            if (motion_start>=0 && frame_number>=motion_start) {
                for (unsigned p=0x224;p<0x2fc && machine.bus.ram[p]!=0xff;p+=12) {
                    const uint8_t *actor=machine.bus.ram+p;
                    if (actor[0]!=machine.bus.ram[0xd8]) continue;
                    fprintf(stderr,"motion %d %u %u %u %u %u %u\n",frame_number,(p-0x224)/12,
                            actor[3]&31,actor[4],actor[10]*256+actor[11],actor[2],actor[8]);
                }
            }
            if (walk_start >= 0 && frame_number >= walk_start &&
                (old_lane != machine.bus.ram[0xc4] || old_depth != machine.bus.ram[0xc6]))
                fprintf(stderr, "walk %d %u %u\n", frame_number,
                    machine.bus.ram[0xc4], machine.bus.ram[0xc6]);
            frame_number++;
            if (machine.cpu.trapped) return 1;
        }
    }
    if (read != EOF) return 2;
    if (getenv("GAMECOM_TRACE_ACTORS")) {
        for (int p=0x224; p<0x2fc && machine.bus.ram[p]!=0xff; p+=12) {
            fprintf(stderr,"actor %d",(p-0x224)/12);
            for (int i=0; i<12; i++) fprintf(stderr," %02x",machine.bus.ram[p+i]);
            fputc('\n',stderr);
        }
    }
    if (getenv("GAMECOM_TRACE_ITEMS")) {
        fprintf(stderr,"inventory room=%u lane=%u depth=%u key=%u armor=%u items=",
            machine.bus.ram[0xc2],machine.bus.ram[0xc4],machine.bus.ram[0xc6],
            !!(machine.bus.ram[0xdb]&0x20),machine.bus.ram[0x21e]);
        for (int p=0x300; p<0x364 && machine.bus.ram[p]!=0xff; p+=5)
            fputc(machine.bus.ram[p]==0xfe ? '1' : '0',stderr);
        fputc('\n',stderr);
    }
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
