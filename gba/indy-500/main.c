/* Indy 500 on GBA: the cartridge and BIOS code run on the native SM8500 core,
 * blits are presented with GBA BG and OBJ layers. */
#include "../gamecom.h"
#include "../sm8500.h"
#include "../video.h"
#include "../pcm_isr.h"
#include "entry_state.h"

#define REG16(a) (*(volatile uint16_t *)(a))

typedef struct {
    char magic[4];
    uint32_t cart, cart_size, bios, palette, bezel;
} assets_t;

extern const assets_t assets;

static void start_cartridge(void) {
    for (unsigned i = 0; i < sizeof entry_ram / sizeof entry_ram[0]; ++i) {
        uint16_t a = entry_ram[i].address;
        if (a >= 0xe000) gc_ext[a - 0xe000] = entry_ram[i].value;
        else gc_ram[a] = entry_ram[i].value;
    }
    sm8500_reset_state();
    for (unsigned i = 0; i < sizeof entry_registers / sizeof entry_registers[0]; ++i)
        cpu.reg[entry_registers[i].address] = entry_registers[i].value;
    cpu.pc = ENTRY_PC;
    cpu.sp = ENTRY_SP;
    cpu.ps0 = ENTRY_PS0;
    cpu.ps1 = ENTRY_PS1;
    cpu.sys = ENTRY_SYS;
    /* Re-map the MMU windows from the restored registers. */
    for (uint16_t a = 0x25; a <= 0x28; ++a) gc_write(a, gc_ram[a]);
}

static void read_keys(void) {
    uint16_t k = (uint16_t)~REG16(0x04000130) & 0x3ff;
    uint8_t in0 = 0xff, in1 = 0xff, in2 = 0xff;
    if (k & 0x0040) in0 &= (uint8_t)~0x01;   /* Up */
    if (k & 0x0080) in0 &= (uint8_t)~0x02;   /* Down */
    if (k & 0x0020) in0 &= (uint8_t)~0x04;   /* Left */
    if (k & 0x0010) in0 &= (uint8_t)~0x08;   /* Right */
    if (k & 0x0004) in0 &= (uint8_t)~0x10;   /* Select: MENU */
    if (k & 0x0008) in0 &= (uint8_t)~0x20;   /* Start: PAUSE */
    if (k & 0x0001) in0 &= (uint8_t)~0x80;   /* A */
    if (k & 0x0002) in1 &= (uint8_t)~0x01;   /* B */
    if (k & 0x0200) in1 &= (uint8_t)~0x02;   /* L: C */
    if (k & 0x0100) in2 &= (uint8_t)~0x02;   /* R: D */
    gc_set_input(in0, in1, in2);
}

volatile uint32_t indy_trap_pc;

/* The kernel PCM handler at 2:357F and Indy 500's copy at 80:5C7E share the kernel table at 36D4. */
static const pcm_isr_t indy_pcm[] = {
    { 0x357f, 0, 0x36d4, 0x35dc, 0x35fa, 1, 0 },
    { 0x5c7e, 0x80, 0x36d4, 0x5cdd, 0x5cfb, 0, 1 },
    { 0 } };

void play(void) {
    const uint8_t *base = (const uint8_t *)&assets;
    gc_media_t media = { base + assets.cart, assets.cart_size - 1, base + assets.bios };
    video_init((const uint16_t *)(base + assets.palette), base + assets.bezel);
    gc_init(&media);
    start_cartridge();
    gc_pcm_isr = indy_pcm;
    for (;;) {
        read_keys();
        gc_run_frame();
        if (cpu.trapped) indy_trap_pc = cpu.trap_pc;
        video_present();
    }
}
