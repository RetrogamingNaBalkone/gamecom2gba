/* Trace only cartridge activity; clean-boot BIOS music uses the same registers. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sys/gcsystem.h"

static gcsystem_t machine;
static unsigned frame;
static unsigned frequency_writes, active_channels, active_scores;
static unsigned sample_requests;
static int log_route;
static int log_calls;

static unsigned stack_byte(unsigned offset)
{
    unsigned mask=machine.cpu.sys&0x40 ? 0xffff : 0xff;
    return gcbus_read(&machine.bus,(machine.cpu.sp+offset)&mask);
}

static unsigned stack_word(unsigned offset)
{
    return stack_byte(offset)*256+stack_byte(offset+1);
}

static uint8_t sound_read(void *ctx, uint16_t address)
{
    const uint8_t *r=machine.bus.ram;
    if (address==0x7122 && machine.cpu.pc==0x7123 && r[0x26]==0x20 && r[0x27]==0x21) {
        unsigned caller=stack_word(0);
        unsigned bank=r[0x27];
        if (caller==0x42f9) {
            caller=stack_word(3);
            bank=caller<0x6000?r[0x26]:stack_byte(2);
        }
        unsigned id=machine.cpu.reg[(machine.cpu.ps0&0xf8)+3];
        printf("frame=%u request=%02x caller=%02x:%04x priority=%02x room=%02x:%02x "
               "scene=%02x slot=%02x player=%02x/%02x weapon=%02x inventory=%02x "
               "foe=%02x/%02x/%02x ui=%02x/%02x/%02x/%02x\n",
               frame,id,bank,caller,r[0xca],r[0x208],r[0x209],r[0xcc],r[0xcd],
               r[0x236],r[0x237],r[0x239],r[0x270],r[0x217],r[0x21b],r[0x21c],
               r[0x261],r[0x26b],r[0x269],r[0x26a]);
    }
    return gcbus_read(ctx,address);
}

static void sound_write(void *ctx, uint16_t address, uint8_t value)
{
    gcbus_write(ctx, address, value);
    uint8_t *r = machine.bus.ram;
    if (r[0x26] != 0x20) return;
    if (address == 0xca && machine.cpu.pc == 0x7136 && r[0x27] == 0x21) {
        if (value<24) sample_requests|=1u<<value;
        if (!log_route && !log_calls)
            printf("frame=%u sample=%u scene=%02x slot=%02x\n",frame,value+1,r[0xcc],r[0xcd]);
    }
    if (address >= 0x46 && address <= 0x49) ++frequency_writes;
    if (address == 0x40 && (value & 3)) ++active_channels;
    if (address == 0x169 && (value & 0x11)) ++active_scores;
}

int main(int argc, char **argv)
{
    if (argc!=4 && (argc!=5 || (strcmp(argv[4],"--route") && strcmp(argv[4],"--calls")))) return 2;
    log_route=argc==5 && !strcmp(argv[4],"--route");
    log_calls=argc==5 && !strcmp(argv[4],"--calls");
    gcsystem_init(&machine);
    if (gcsystem_load_internal(&machine, argv[1]) ||
        gcsystem_load_external(&machine, argv[2]) ||
        gcsystem_load_cart(&machine, argv[3])) return 1;
    gcsystem_reset(&machine);
    machine.cpu.wr = sound_write;
    if (log_calls) machine.cpu.rd = sound_read;
    unsigned duration, in0, in1, in2;
    int column, row;
    unsigned room=~0u, ending=0;
    while (scanf("%u%x%x%x%d%d", &duration,&in0,&in1,&in2,&column,&row)==6) {
        gcbus_set_buttons(&machine.bus,in0,in1,in2);
        for (int c=0;c<13;++c) gcbus_set_touch(&machine.bus,c,c==column ? 1u<<row : 0);
        for (unsigned i=0;i<duration;i++) {
            ++frame;
            gcsystem_run_frame(&machine);
            if (machine.cpu.trapped) return 1;
            const uint8_t *r=machine.bus.ram;
            if (log_route && r[0x26]==0x20 && r[0x232]==2) {
                unsigned next=(unsigned)r[0x208]*256+r[0x209];
                unsigned complete=r[0xc2]&1;
                if (next!=room || (complete && !ending)) {
                    printf("frame=%u room=%02x:%02x hp=%u ammo=%u/%u flags=%02x/%02x/%02x ending=%u\n",
                           frame,r[0x208],r[0x209],r[0x238],r[0x295],r[0x296],
                           r[0xd3],r[0xd4],r[0xd5],complete);
                    room=next;
                }
                ending=complete;
            }
        }
    }
    printf("frames=%u frequency_writes=%u wave_enables=%u score_enables=%u\n",
           frame,frequency_writes,active_channels,active_scores);
    printf("Requested PCM IDs (hex):");
    for (unsigned i=0;i<24;++i)
        if (sample_requests&(1u<<i)) printf(" %02X",i+1);
    putchar('\n');
    return 0;
}
