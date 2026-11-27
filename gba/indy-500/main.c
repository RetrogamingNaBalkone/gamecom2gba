/* Indy 500 on GBA: the cartridge and BIOS code run on the native SM8500 core,
 * blits are presented with GBA BG and OBJ layers. */
#include "../gamecom.h"
#include "../sm8500.h"
#include "../video.h"
#include "launch.h"

#define REG16(a) (*(volatile uint16_t *)(a))

typedef struct {
    char magic[4];
    uint32_t cart, cart_size, bios, palette, bezel;
} assets_t;

extern const assets_t assets;

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

void play(void) {
    const uint8_t *base = (const uint8_t *)&assets;
    gc_media_t media = { base + assets.cart, assets.cart_size - 1, base + assets.bios };
    video_init((const uint16_t *)(base + assets.palette), base + assets.bezel);
    gc_init(&media);
    indy_launch(3);
    for (;;) {
        read_keys();
        gc_run_frame();
        if (cpu.trapped) indy_trap_pc = cpu.trap_pc;
        video_present();
    }
}
