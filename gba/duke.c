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

static void full_scene(const uint8_t *tiles) {
    copy_words(tiles,0x06000000,4800);
    volatile uint16_t *map=(volatile uint16_t *)0x0600f800;
    for (unsigned y=0; y<20; y++)
        for (unsigned x=0; x<30; x++) map[y*32+x]=30*y+x;
}

struct Atlas {
    const uint8_t *backgrounds, *sprites;
};

static uint32_t view_entry(const uint8_t *room,unsigned lane,unsigned depth) {
    const uint32_t *offsets=(const uint32_t *)(room+36+4*room[32]);
    return offsets[depth*room[0]+lane-1];
}

static void show_view(const struct Atlas *atlas, const uint8_t *room, unsigned lane, unsigned depth,
                      unsigned open_doors, uint32_t taken) {
    unsigned offset=view_entry(room,lane,depth)&0x7fffffff;
    if (open_doors && depth+1==room[1]) {
        for (int i=0; i<3; i++)
            if ((open_doors & (1u<<i)) && (room[4+4*i]&31)==lane)
                offset=((const uint32_t *)(room+20))[i];
    }
    const uint32_t *view=(const uint32_t *)(room+offset);
    unsigned map_words=17*32/2;
    const uint32_t *tiles=view+1+map_words;
    for (unsigned i=0; i<*view; ) {
        unsigned start=i++;
        while (i<*view && tiles[i]==tiles[i-1]+1) i++;
        copy_words(atlas->backgrounds+32*tiles[start],0x06000000+32*start,8*(i-start));
    }
    copy_words(view+1,0x0600f800,map_words);
    const uint32_t *objects=tiles+*view;
    unsigned count=*objects++;
    volatile uint16_t *oam=(volatile uint16_t *)0x07000008;
    unsigned slot=0;
    for (unsigned i=0; i<count; i++,objects+=2) {
        const uint8_t *sprite=(const uint8_t *)objects;
        if (taken & (1u<<sprite[0])) continue;
        unsigned tile=384+16*slot++;
        copy_words(atlas->sprites+512*objects[1],0x06010000+32*tile,128);
        oam[0]=sprite[2];
        oam[1]=0x8000|sprite[1];
        oam[2]=tile;
        oam+=4;
    }
    for (; slot<40; slot++,oam+=4) oam[0]=0x200;
}

/* Timer 2 ticks at 16,384 Hz. The opening pistol trace averages 16.3
 * display periods between shots and 78 from the sixth shot to the next.
 * Presentation can miss a VBlank without advancing the weapon clock twice.
 */
enum { SHOT = 4470, FLASH = 2194, RELOAD = 21395 };
/* Nine lateral steps in the reference take 118 display intervals from
 * first to last (14.75 per step). Timer 2 runs at 16,384 Hz. */
enum { STREET_STEP = 4046, LOBBY_STEP = 4320 };
struct Weapon {
    unsigned remaining, magazine, delay, age, pose;
};

struct Inventory {
    unsigned key, medkit, health, shells, rockets, weapons;
};

static unsigned capped(unsigned value,unsigned limit) {
    return value<limit ? value : limit;
}

static uint32_t collect(const uint8_t *room, unsigned lane, unsigned depth, uint32_t taken,
                        struct Inventory *inv, struct Weapon *gun) {
    for (unsigned i=0; i<room[32]; i++) {
        const uint8_t *item=room+36+4*i;
        if ((taken & (1u<<i)) || (unsigned)(item[0]-depth)>1) continue;
        int x=item[2]+50*((int)item[1]-(int)lane);
        if (item[1]!=lane && item[1]==1) x+=22;
        if (item[1]!=lane && item[1]==room[0]) x-=22;
        if (x<=20 || x>=180) continue;
        switch (item[3]) {
            case 1: if (inv->key) continue; inv->key=1; break;
            case 2: if (inv->medkit==100) continue; inv->medkit=100; break;
            case 3: if (inv->health==100) continue; inv->health=capped(inv->health+30,100); break;
            case 4: case 5:
                if (!(item[3]==4 && !(inv->weapons&4)) && inv->rockets==50) continue;
                if (item[3]==4) inv->weapons|=4;
                inv->rockets=capped(inv->rockets+5,50);
                break;
            case 6: case 9:
                if (!(item[3]==9 && !(inv->weapons&2)) && inv->shells==50) continue;
                if (item[3]==9) inv->weapons|=2;
                inv->shells=capped(inv->shells+6,50);
                break;
            case 7: case 8:
                if (!(item[3]==8 && !(inv->weapons&1)) && gun->remaining==200) continue;
                if (item[3]==8) inv->weapons|=1;
                gun->remaining=capped(gun->remaining+12,200);
                break;
            default: continue;
        }
        taken|=1u<<i;
    }
    return taken;
}

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
    const uint8_t *campaign=assets + 32 + 2*19200 + 6*2048;
    const uint32_t *levels=(const uint32_t *)campaign;
    struct Atlas atlas={campaign+levels[2+levels[0]],campaign+levels[3+levels[0]]};
    unsigned level=0;
    unsigned next_level=1;
    int exit_active=0;
    unsigned exit_time=0, exit_screen=0;
    const uint8_t *rooms=campaign+levels[1];
    unsigned room_count=*(const uint32_t *)rooms;
    uint32_t taken[256];
    uint8_t unlocked[256];
    for (unsigned i=0; i<room_count; i++) { taken[i]=0; unlocked[i]=0; }
    struct Inventory inventory={0,0,100,0,0,1};
    unsigned room_id=0;
    const uint32_t *directory=(const uint32_t *)rooms+1;
    const uint8_t *room=rooms+directory[0];
    unsigned open_doors=0;
    unsigned lane=room[2], depth=0, walk_delay=0;
    show_view(&atlas,room,lane,depth,0,taken[0]);
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
        if (exit_active) {
            old_keys=keys;
            exit_time+=dt;
            /* The reference trace reaches the score screen after 292 LCD
               periods, the next level title after 445, and play after 565. */
            unsigned screen=exit_time>=122100 ? 3 : exit_time>=80100 ? 2 : exit_time>=19200 ? 1 : 0;
            if (screen!=exit_screen) {
                REG16(0x04000000)=0x0100;
                unsigned card_level=screen==3 ? next_level : level;
                full_scene(campaign+levels[1+levels[0]]+19200*(3*card_level+screen-1));
                exit_screen=screen;
            }
            if (exit_time<155000) continue;
            level=next_level;
            rooms=campaign+levels[1+level];
            directory=(const uint32_t *)rooms+1;
            room_count=directory[-1];
            room_id=0;
            room=rooms+directory[0];
            for (unsigned i=0; i<room_count; i++) { taken[i]=0; unlocked[i]=0; }
            lane=room[2];
            depth=0;
            open_doors=0;
            inventory.key=0;
            walk_delay=0;
            exit_active=0;
            entering=1;
            full_scene(assets+32+19200);
            show_view(&atlas,room,lane,depth,0,0);
            displayed=255;
            REG16(0x04000000)=0x1140;
            continue;
        }
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
                        if (pressed&B) {
                            unsigned bit=1u<<i;
                            if ((spec&128) && !(unlocked[room_id]&bit)) {
                                if (inventory.key) {
                                    inventory.key=0;
                                    unlocked[room_id]|=bit;
                                }
                            } else {
                                open_doors ^= bit;
                                show_view(&atlas,room,lane,depth,open_doors,taken[room_id]);
                                if (room[5+4*i]==254 && (open_doors&bit)) {
                                    next_level=level+1;
                                    if (next_level==4 || next_level==9 || next_level==14) next_level++;
                                    if (next_level==levels[0]) next_level=0;
                                    exit_active=1;
                                    exit_time=0;
                                    exit_screen=0;
                                }
                            }
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
                        else if (room[0]==1 && room[1]>1 && (room[3]&0x30) && room[5]<room_count) {
                            room_id=room[5];
                            room=rooms+directory[room_id];
                            x=1;
                            z=0;
                        } else if (door>=0 && (!!(open_doors & (1u<<door)) != !(room[4+4*door]&0x60))) {
                            const uint8_t *exit=room+4+4*door;
                            if (exit[1]<room_count) {
                                room_id=exit[1];
                                room=rooms+directory[room_id];
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
                                room_id=back;
                                room=rooms+directory[room_id];
                                x=1;
                                z=room[1]-1;
                            }
                        }
                        break;
                    case LEFT: case RIGHT:
                        if (room[3]==0x30 && room[1]==1) {
                            const uint8_t *branch=room+(direction==LEFT ? 8 : 4);
                            if (branch[1]<room_count) {
                                room_id=branch[1];
                                room=rooms+directory[room_id];
                                x=branch[2];
                                z=0;
                            }
                        } else if (direction==LEFT && x>1) x--;
                        else if (direction==RIGHT && x<room[0]) x++;
                        break;
                }
                if (old_room==room && (view_entry(room,x,z)&0x80000000u)) {
                    x=lane;
                    z=depth;
                }
                if (old_room!=room) open_doors=0;
                if (lane!=x || depth!=z || old_room!=room) {
                    taken[room_id]=collect(room,x,z,taken[room_id],&inventory,&w);
                    show_view(&atlas,room,x,z,open_doors,taken[room_id]);
                    lane=x;
                    depth=z;
                }
                unsigned overdue=walk_delay ? dt-walk_delay : 0;
                unsigned step=room_id==1 ? LOBBY_STEP : STREET_STEP;
                walk_delay=overdue < step ? step-overdue : 0;
            } else walk_delay=0;
        }
        unsigned pose = w.age < FLASH ? w.pose : 0;
        if (w.pose >= 2 && w.age >= FLASH && w.age < SHOT) pose = w.pose+1;
        REG16(0x07000004) = pose*64;
        if (displayed != w.remaining) {
            /* Exact for the byte-sized ammunition counter on ARM7TDMI. */
            unsigned tens = (w.remaining * 205u) >> 11;
            unsigned ones = w.remaining - 10*tens;
            unsigned hundreds=(tens*205u)>>11;
            digit(20+149, hundreds);
            digit(20+157, tens-10*hundreds);
            digit(20+165, ones);
            displayed = w.remaining;
        }
    }
}
