/* Independent worst-case video workload; no game logic or cartridge data. */
#include "video.h"

#define REG16(address) (*(volatile uint16_t *)(address))

/* The host observes this diagnostic-only mailbox through mGBA's memory map. */
struct Sample {
    uint32_t frames, interval, copy_end_line, error;
};
static volatile struct Sample *const sample = (void *)0x03007000;
static const uint16_t colors[16] = {0, 0x001f, 0x03e0, 0x7c00};
static const uint16_t bezel[9600] = {0};
static const uint16_t tiles[512 * 16] = {
    [0 ... 15] = 0x1111, [511 * 16 ... 512 * 16 - 1] = 0x3333
};
static const uint16_t objects[768 * 16] = {[0 ... 768 * 16 - 1] = 0x2222};
static uint16_t map[500] __attribute__((section(".ewram")));

void play(void)
{
    re2_video_init(colors, (const uint8_t *)bezel);
    sample->frames = sample->interval = sample->copy_end_line = sample->error = 0;
    REG16(0x04000100) = 0;
    REG16(0x04000102) = 0x81; /* 1 counter tick per 64 CPU cycles. */
    uint16_t previous = 0;
    for (unsigned frame = 1; ; ++frame) {
        for (unsigned i = 0; i < 500; ++i) map[i] = frame & 1 ? 511 : 0;
        if (!re2_video_room((const uint8_t *)tiles, 512, map)) sample->error |= 1;
        for (unsigned i = 0; i < 64; ++i)
            if (!re2_video_obj_tiles(i * 12, (const uint8_t *)(objects + i * 192), 12))
                sample->error |= 2;
        if (re2_video_obj_tiles(0, (const uint8_t *)objects, 1)) sample->error |= 4;
        for (unsigned i = 0; i < 128; ++i)
            re2_video_sprite(i, (i % 16) * 8 - (int)(frame & 1), (i / 16) * 12,
                             i, 0, 0, 0);
        re2_video_present();
        uint16_t now = REG16(0x04000100);
        sample->copy_end_line = REG16(0x04000006);
        sample->interval = (uint16_t)(now - previous);
        sample->frames = frame;
        previous = now;
    }
}
