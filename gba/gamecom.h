/* game.com machine model for native GBA ports: memory map, I/O, timers and frame stepping. */
#ifndef GAMECOM_H
#define GAMECOM_H
#include <stdint.h>

#define GC_CYCLES_PER_SECOND 4915200
#define GC_CYCLES_PER_FRAME 82287       /* 4.9152 MHz / 59.732155 Hz */

typedef struct {
    const uint8_t *cart;      /* cartridge image */
    uint32_t cart_mask;       /* size - 1 */
    const uint8_t *bios;      /* external BIOS, 256 KiB */
} gc_media_t;

typedef struct {
    uint8_t dmc, sx, sy, w, h, dx, dy, pal, bank, page, lch;
} gc_blit_t;

extern uint8_t gc_ram[0x1000];      /* I/O registers and internal RAM 0000-0FFF */
extern uint8_t gc_ext[0x2000];      /* extended RAM E000-FFFF */
extern gc_media_t gc_media;
extern int gc_idle_hints;   /* replace known polling loops by HALT */

/* Button state, active low, as read through the key matrix (see gc_set_input). */
void gc_set_input(uint8_t in0, uint8_t in1, uint8_t in2);
void gc_set_touch(int column, uint16_t rows);
void gc_init(const gc_media_t *media);
/* Runs one 59.73 Hz display period of emulated time. */
void gc_run_frame(void);
uint8_t gc_lcdc(void);
/* Restores the timing phase of a captured cartridge launch (see indy-500/entry_state.h). */
void gc_restore_timing(int frame_cycle, uint32_t clock, const uint8_t reloads[2], const int counts[2],
                       int busy, uint16_t pending);

/* Provided by the renderer. */
void video_blit(const gc_blit_t *blit, const uint8_t *source, uint16_t source_mask, int source_width);
void video_vram_write(uint16_t offset, uint8_t value);
void video_vram_copy(const gc_blit_t *blit);

#endif
