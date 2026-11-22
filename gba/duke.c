#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
enum { A = 1, START = 8, R = 256, L = 512 };

/* Timer 2 ticks at 16,384 Hz. The opening pistol trace averages 16.3
 * display periods between shots and 78 from the sixth shot to the next.
 * Presentation can miss a VBlank without advancing the weapon clock twice.
 */
enum { SHOT = 4470, FLASH = 2194, RELOAD = 21395 };
struct Weapon {
    unsigned remaining, magazine, delay, age, pose;
};

static void advance(struct Weapon *w, unsigned dt, unsigned keys) {
    if (w->age < SHOT) w->age += dt;
    if (dt < w->delay) {
        w->delay -= dt;
        return;
    }
    unsigned overdue = dt - w->delay;
    w->delay = 0;
    if (!(keys & (A|L|R)) || !w->remaining) return;
    if (!w->magazine) w->magazine = 6;
    w->remaining--;
    w->magazine--;
    w->pose = keys & A ? 1 : keys & L ? 2 : 4;
    unsigned interval = w->magazine ? SHOT : RELOAD;
    w->age = overdue;
    w->delay = overdue < interval ? interval - overdue : 0;
}

static const uint8_t digits[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};

/* BG0's HUD cells use dedicated tiles. Change only the affected glyphs. */
static void digit(unsigned x, unsigned value) {
    volatile uint16_t *tiles = (volatile uint16_t *)0x06000000;
    for (unsigned row=0; row<5; row++) {
        unsigned y = 151 + row;
        for (unsigned col=0; col<3; col++) {
            unsigned px = x + col;
            unsigned tile = (y/8)*30 + px/8;
            unsigned index = tile*16 + (y%8)*2 + (px%8)/4;
            unsigned shift = (px%4)*4;
            unsigned ink = (digits[value][row] & (4u>>col)) ? 1 : 4;
            tiles[index] = (tiles[index] & ~(15u << shift)) | (ink << shift);
        }
    }
}

void play(void) {
    struct Weapon w = {48, 6, 0, FLASH*2, 0};
    REG16(0x0400010a) = 0x83;
    unsigned previous = REG16(0x04000108);
    unsigned old_keys = START | A;
    unsigned displayed = 255;
    int paused = 0;
    int entering = 1;
    for (;;) {
        while (REG16(0x04000006) >= 160) {}
        while (REG16(0x04000006) < 160) {}
        unsigned now = REG16(0x04000108);
        unsigned dt = (uint16_t)(now - previous);
        previous = now;
        unsigned keys = ~REG16(0x04000130) & 1023;
        if (entering) {
            entering = (keys & (A|START)) != 0;
            keys &= ~(A|START);
        }
        if ((keys & ~old_keys) & START) paused = !paused;
        old_keys = keys;
        if (!paused) advance(&w, dt, keys);
        unsigned pose = w.age < FLASH ? w.pose : 0;
        if (w.pose >= 2 && w.age >= FLASH && w.age < SHOT) pose = w.pose+1;
        REG16(0x07000004) = pose*64;
        if (displayed != w.remaining) {
            /* Exact for the byte-sized ammunition counter on ARM7TDMI. */
            unsigned tens = (w.remaining * 205u) >> 11;
            unsigned ones = w.remaining - 10*tens;
            digit(20+157, tens);
            digit(20+165, ones);
            displayed = w.remaining;
        }
    }
}
