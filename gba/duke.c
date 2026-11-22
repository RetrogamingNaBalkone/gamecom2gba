#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
enum { A = 1, B = 2, START = 8, R = 256, L = 512 };
enum { RIGHT = 16, LEFT = 32, UP = 64, DOWN = 128 };
extern const uint8_t assets[];

static void copy_words(const void *src, uintptr_t dest, unsigned count) {
    *(volatile uintptr_t *)0x040000d4 = (uintptr_t)src;
    *(volatile uintptr_t *)0x040000d8 = dest;
    *(volatile uint32_t *)0x040000dc = 0x84000000u | count;
}

static void show_view(const uint8_t *room, unsigned lane, unsigned depth, unsigned open_doors) {
    const uint32_t *offsets=(const uint32_t *)(room+32);
    unsigned offset=offsets[depth*room[0]+lane-1];
    if (open_doors && depth+1==room[1]) {
        for (int i=0; i<3; i++)
            if ((open_doors & (1u<<i)) && (room[4+4*i]&31)==lane)
                offset=((const uint32_t *)(room+20))[i];
    }
    const uint32_t *view=(const uint32_t *)(room+offset);
    unsigned map_words=17*32/2;
    copy_words(view+1+map_words,0x06000000,*view/4);
    copy_words(view+1,0x0600f800,map_words);
}

/* Timer 2 ticks at 16,384 Hz. The opening pistol trace averages 16.3
 * display periods between shots and 78 from the sixth shot to the next.
 * Presentation can miss a VBlank without advancing the weapon clock twice.
 */
enum { SHOT = 4470, FLASH = 2194, RELOAD = 21395 };
/* Nine lateral steps in the reference take 118 display intervals from
 * first to last (14.75 per step). Timer 2 runs at 16,384 Hz. */
enum { WALK_STEP = 4046 };
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
    const uint8_t *rooms = assets + 32 + 2*19200 + 6*2048;
    unsigned room_count=*(const uint32_t *)rooms;
    const uint32_t *directory=(const uint32_t *)rooms+1;
    const uint8_t *room=rooms+directory[0];
    unsigned open_doors=0;
    unsigned lane=room[2], depth=0, walk_delay=0;
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
        unsigned pressed=keys & ~old_keys;
        if (pressed & START) paused = !paused;
        old_keys = keys;
        if (!paused) {
            advance(&w, dt, keys);
            unsigned direction=keys & (UP|DOWN|LEFT|RIGHT);
            int door=-1;
            if (depth+1 == room[1]) {
                for (int i=0; i<3; i++) {
                    unsigned spec=room[4+4*i];
                    if (spec!=255 && (spec&31)==lane) {
                        door=i;
                        if ((pressed&B) && !(spec&128)) {
                            open_doors ^= 1u<<i;
                            show_view(room,lane,depth,open_doors);
                        }
                        break;
                    }
                }
            }
            if (walk_delay > dt) walk_delay-=dt;
            else if (direction) {
                unsigned x=lane, z=depth;
                const uint8_t *old_room=room;
                switch (direction) {
                    case UP:
                        if (z+1<room[1]) z++;
                        else if (room[0]==1 && (room[3]&0x30) && room[5]<room_count) {
                            room=rooms+directory[room[5]];
                            x=1;
                            z=0;
                        } else if (door>=0 && (open_doors & (1u<<door))) {
                            const uint8_t *exit=room+4+4*door;
                            if (exit[1]<room_count) {
                                room=rooms+directory[exit[1]];
                                x=exit[2];
                                z=0;
                                open_doors=0;
                            }
                        }
                        break;
                    case DOWN:
                        if (z>0) z--;
                        else {
                            unsigned back=room[16];
                            if (room[17]!=255 && x!=1)
                                back=x==room[0] ? room[17] : 255;
                            if (back<room_count) {
                                room=rooms+directory[back];
                                x=1;
                                z=room[1]-1;
                            }
                        }
                        break;
                    case LEFT: if (x>1) x--; break;
                    case RIGHT: if (x<room[0]) x++; break;
                }
                if (old_room!=room) open_doors=0;
                if (lane!=x || depth!=z || old_room!=room) {
                    show_view(room,x,z,open_doors);
                    lane=x;
                    depth=z;
                }
                unsigned overdue=walk_delay ? dt-walk_delay : 0;
                walk_delay=overdue < WALK_STEP ? WALK_STEP-overdue : 0;
            } else walk_delay=0;
        }
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
