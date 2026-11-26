#include "progress.h"
#include "puzzle.h"

enum { FLAGS=0xd3, EVENTS=0xd4, ROOM=0x209, X=0x234, Y=0x235, FACING=0x233, BAG=0x262 };

static uint8_t *slot(unsigned item)
{
    for (unsigned i=0;i<6;++i)
        if (re2_progress[BAG+i]==item) return &re2_progress[BAG+i];
    return 0;
}

static int scene(unsigned id)
{
    re2_progress[0xcc]=id;
    re2_progress[0xcd]=0;
    re2_progress[0x20b]|=2;
    return 1;
}

static void notify_item(unsigned item)
{
    re2_progress[0x270]=item;
    re2_progress[0xd6]|=0xc0;
}

static int position(unsigned low,unsigned high,unsigned facing)
{
    return low<=re2_progress[X] && re2_progress[X]<high && re2_progress[FACING]==facing;
}

/* Bank 24:6AB8. The three switches must be operated in the same order
 * as on the cartridge, including the latch reset after a wrong switch. */
static int switches(unsigned room)
{
    uint8_t *flags=&re2_progress[FLAGS], *events=&re2_progress[EVENTS];
    if (*events&8) return 0;
    if (room==0x46 && position(145,165,2)) {
        *flags^=4;
        if ((*flags&4) && (*events&2)) *events|=4;
        else *events&=0xf1;
    } else if (room==0x47 && position(15,40,2)) {
        *flags^=8;
        if ((*flags&0x1c)==8) *events|=2;
        else *events&=0xf1;
    } else if (room==0x47 && position(95,115,2)) {
        *flags^=16;
        if ((*flags&16) && (*events&4)) {
            uint8_t *free=slot(0);
            if (free) { *events|=8; *free=0x12; }
            else *events&=0xf1;
            notify_item(0x12);
            re2_progress[X]=150;
            re2_progress[ROOM]=0x45;
            return scene(0x16);
        }
        *events&=0xf1;
    } else return 0;
    return 1;
}

static int police_station(unsigned room)
{
    unsigned item,bit,animation;
    if (room==1 && position(105,145,1)) {
        item=0x17; bit=1; animation=0x12;
    } else if (room==0x20 && position(0,90,0x61) && re2_progress[0x268]==0x0d) {
        item=0x14; bit=2; animation=0x0f;
    } else return switches(room);
    if (re2_progress[FLAGS]&bit) return 0;
    uint8_t *free=slot(0);
    if (free) { *free=item; re2_progress[FLAGS]|=bit; }
    notify_item(item);
    return scene(animation);
}

static int sockets(unsigned room)
{
    if (room==0x35 || room==0x36) {
        unsigned bit=room==0x35 ? 32 : 64;
        unsigned left=room==0x35 ? 153 : 65;
        if ((re2_progress[FLAGS]&bit) || !position(left,left+20,2)) return 0;
        uint8_t *stone=slot(0x14);
        if (!stone) stone=slot(0x1c);
        if (!stone) return 0;
        *stone=0;
        re2_progress[FLAGS]|=bit;
        if ((re2_progress[FLAGS]&0x60)!=0x60) return scene(room==0x35?0x14:0x15);
        *stone=0x15;
        notify_item(*stone);
        return scene(room==0x35?0x25:0x26);
    }
    if ((room!=0x49 && room!=0x4c) || !position(92,108,2)) return 0;
    uint8_t *flags=&re2_progress[room==0x49?FLAGS:EVENTS];
    unsigned bit=room==0x49?128:1;
    uint8_t *item=slot(room==0x49?0x13:0x12);
    if ((*flags&bit) || !item) return 0;
    *item=0;
    *flags|=bit;
    return scene(room==0x49?0x10:0x11);
}

static int area_four(unsigned room)
{
    unsigned animation,cam,x,face;
    if (room==0x1b && position(85,100,1)) {
        uint8_t *item=slot(0x1b);
        if (!item) return 0;
        *item=0;
        re2_progress[0x20b]|=4;
        animation=0x0d; cam=0x20; x=100; face=0x42;
    } else if (room==0x1b && re2_progress[X]>=60 && re2_progress[X]<80) {
        /* At 24:69D0 the facing compare is followed directly by the scene
           assignment, so this interaction also works while facing away. */
        animation=7; cam=0x20; x=110; face=0x42;
    } else if (room==0x28 && position(140,180,1)) {
        animation=10; cam=0x29; x=60; face=0x22;
    } else return 0;
    re2_progress[ROOM]=cam;
    re2_progress[X]=x;
    re2_progress[Y]=97;
    re2_progress[FACING]=face;
    return scene(animation);
}

int re2_puzzle_action(void)
{
    unsigned room=re2_progress[ROOM];
    switch (re2_progress[0x208]) {
    case 1: return police_station(room);
    case 4: return area_four(room);
    case 5: return sockets(room);
    default: return 0;
    }
}
