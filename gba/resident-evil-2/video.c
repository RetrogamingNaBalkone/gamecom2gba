#include "video.h"

#define REG16(address) (*(volatile uint16_t *)(address))
#define EWRAM __attribute__((section(".ewram"), aligned(4)))
enum { VRAM = 0x06000000, OAM = 0x07000000, SCANLINE = 0x04000006 };
enum { MAP_BEZEL = 29, MAP_ROOM = 30 };

struct Transfer {
    const uint8_t *source;
    unsigned first, count;
};

static struct Transfer transfers[64] EWRAM;
static uint16_t objects[128][4] EWRAM;
static unsigned transfer_count EWRAM, queued_tiles EWRAM;
static unsigned active_bank EWRAM, room_pending EWRAM, objects_dirty EWRAM;
static unsigned front_pending EWRAM, display EWRAM;
static uint16_t lcd_colors[4];
static const uint8_t *overlay_tiles,*deferred_tiles;
static const uint16_t *overlay_map,*deferred_map;
static const uint32_t *deferred_runs;
static unsigned deferred_count;
static unsigned overlay_priority;
static unsigned palette_pending, lcd_mapping;

void re2_video_lcd_palette(unsigned mapping)
{
    lcd_mapping=mapping;
    palette_pending=1;
}

static void copy16(const void *source, uintptr_t destination, unsigned halfwords)
{
    *(volatile uintptr_t *)0x040000d4 = (uintptr_t)source;
    *(volatile uintptr_t *)0x040000d8 = destination;
    *(volatile uint32_t *)0x040000dc = 0x80000000u | halfwords;
}

void re2_video_init(const uint16_t palette[16], const uint8_t *bezel_4bpp)
{
    REG16(0x04000000) = 0x80;
    copy16(palette, 0x05000000, 16);
    copy16(palette, 0x05000200, 16);
    for (unsigned i=0;i<4;++i) lcd_colors[i]=palette[1+i];
    volatile uint16_t *map = (volatile uint16_t *)(VRAM + MAP_BEZEL * 2048);
    for (unsigned y = 0; y < 20; ++y) {
        for (unsigned side = 0; side < 2; ++side) {
            unsigned column = side * 27;
            unsigned index = y * 6 + side * 3;
            copy16(bezel_4bpp + (30 * y + column) * 32,
                   VRAM + 0x8000 + index * 32, 48);
            for (unsigned i = 0; i < 3; ++i)
                map[32 * y + column + i] = index + i;
        }
    }
    REG16(0x04000008) = (MAP_BEZEL << 8) | (2 << 2);
    REG16(0x0400000a) = 0;
    REG16(0x04000010) = REG16(0x04000012) = 0;
    REG16(0x04000014) = 512 - 20;
    REG16(0x04000016) = 0;
    REG16(0x04000040) = (20 << 8) | 220;
    REG16(0x04000044) = 160;
    REG16(0x04000018) = 512 - 20;
    REG16(0x0400001a) = 0;
    REG16(0x0400001c) = 512 - 20;
    REG16(0x0400001e) = 0;
    REG16(0x04000048) = 0x1e; /* Room, foreground, overlay and OBJ inside the LCD. */
    REG16(0x0400004a) = 0x01; /* Only the bezel outside. */
    for (unsigned i = 0; i < 128; ++i) {
        objects[i][0] = 0x200;
        objects[i][1] = objects[i][2] = objects[i][3] = 0;
    }
    active_bank = transfer_count = queued_tiles = room_pending = 0;
    front_pending = 0;
    overlay_tiles=deferred_tiles=0;
    display = 0x3140;
    objects_dirty = 1;
}

static void load_tiles(const uint8_t *tiles,const uint32_t *runs,uintptr_t dest,unsigned count)
{
    if (!runs) {
        copy16(tiles,dest,count*16);
        return;
    }
    while (count) {
        uint32_t run=*runs++;
        unsigned size=1+(run>>24);
        copy16(tiles+32*(run&0xffffff),dest,size*16);
        dest+=size*32;
        count-=size;
    }
}

static void upload_room(const uint8_t *tiles,const uint32_t *runs,unsigned count,const uint16_t *map)
{
    unsigned bank = active_bank ^ 1;
    load_tiles(tiles,runs,VRAM + bank * 0x4000,count);
    uintptr_t destination = VRAM + (MAP_ROOM + bank) * 2048;
    for (unsigned y = 0; y < 20; ++y)
        copy16(map + 25 * y, destination + 64 * y, 25);
}

static int set_room(const uint8_t *tiles,const uint32_t *runs,unsigned count,const uint16_t *map)
{
    if (!count || count > 512) return 0;
    overlay_tiles=0;
    if (display&0x800) {
        deferred_tiles=tiles; deferred_runs=runs; deferred_count=count; deferred_map=map;
    } else upload_room(tiles,runs,count,map);
    room_pending = 1;
    front_pending = 0;
    return 1;
}

void re2_video_overlay(const uint8_t *tiles,const uint16_t *map,unsigned behind_actors)
{
    overlay_tiles=tiles;
    overlay_map=map;
    overlay_priority=behind_actors?2:0;
}

int re2_video_room(const uint8_t *tiles,unsigned count,const uint16_t *map)
{
    return set_room(tiles,0,count,map);
}

static int set_front(const uint8_t *tiles,const uint32_t *runs,unsigned count,const uint16_t *map)
{
    if (!room_pending || !count || count > 292) return 0;
    unsigned bank = active_bank ^ 1;
    unsigned base = 120 + 292 * bank;
    load_tiles(tiles,runs,VRAM + 0x8000 + base * 32,count);
    volatile uint16_t *out = (volatile uint16_t *)(VRAM + (27 + bank) * 2048);
    for (unsigned y = 0; y < 20; ++y)
        for (unsigned x = 0; x < 25; ++x)
            out[32*y+x] = base + map[25*y+x];
    front_pending = 1;
    return 1;
}

int re2_video_front(const uint8_t *tiles,unsigned count,const uint16_t *map)
{
    return set_front(tiles,0,count,map);
}

int re2_video_plane(const uint8_t *atlas,const uint8_t *plane,unsigned foreground)
{
    unsigned count=*(const uint32_t *)plane;
    const uint32_t *runs=(const uint32_t *)(plane+1004);
    const uint16_t *map=(const uint16_t *)(plane+4);
    if (foreground) return set_front(atlas,runs,count,map);
    return set_room(atlas,runs,count,map);
}

int re2_video_obj_tiles(unsigned first, const uint8_t *tiles, unsigned count)
{
    if (!count || count > 768 - queued_tiles || first > 1024 - count ||
        transfer_count == 64) return 0;
    transfers[transfer_count++] = (struct Transfer){tiles, first, count};
    queued_tiles += count;
    return 1;
}

void re2_video_hide(unsigned slot)
{
    if (slot < 128 && objects[slot][0] != 0x200) {
        objects[slot][0] = 0x200;
        objects_dirty = 1;
    }
}

void re2_video_sprite(unsigned slot, int x, int y, unsigned first,
                      unsigned shape, unsigned size, unsigned flags)
{
    static const uint8_t widths[3][4] = {{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const uint8_t heights[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    if (slot >= 128) return;
    if (shape >= 3 || size >= 4 || first >= 1024 ||
        x <= -(int)widths[shape][size] || x >= 200 ||
        y <= -(int)heights[shape][size] || y >= 160 ||
        first + widths[shape][size] * heights[shape][size] / 64 > 1024) {
        re2_video_hide(slot);
        return;
    }
    uint16_t a = (shape << 14) | (y & 255);
    uint16_t b = (size << 14) | ((flags & 3) << 12) | ((x + 20) & 511);
    uint16_t c = (2 << 10) | first;
    if (objects[slot][0] != a || objects[slot][1] != b || objects[slot][2] != c) {
        objects[slot][0] = a;
        objects[slot][1] = b;
        objects[slot][2] = c;
        objects_dirty = 1;
    }
}

void re2_video_present(void)
{
    while (REG16(SCANLINE) >= 160) { }
    while (REG16(SCANLINE) < 160) { }
    if (palette_pending) {
        for (unsigned i=0;i<4;++i) {
            unsigned color=lcd_colors[(lcd_mapping>>(2*i))&3];
            REG16(0x05000002+i*2)=REG16(0x05000202+i*2)=color;
        }
        palette_pending=0;
    }
    for (unsigned i = 0; i < transfer_count; ++i) {
        const struct Transfer *t = &transfers[i];
        copy16(t->source, VRAM + 0x10000 + t->first * 32, t->count * 16);
    }
    transfer_count = queued_tiles = 0;
    if (objects_dirty) {
        copy16(objects, OAM, sizeof(objects) / 2);
        objects_dirty = 0;
    }
    if (room_pending) {
        if (deferred_tiles) {
            upload_room(deferred_tiles,deferred_runs,deferred_count,deferred_map);
            deferred_tiles=0;
        }
        active_bank ^= 1;
        REG16(0x0400000a) = ((MAP_ROOM + active_bank) << 8) | (active_bank << 2) | 3;
        REG16(0x0400000c) = ((27 + active_bank) << 8) | (2 << 2) | 1;
        display = 0x3340 | (front_pending ? 0x400 : 0);
        room_pending = 0;
    }
    if (overlay_tiles) {
        upload_room(overlay_tiles,0,500,overlay_map);
        unsigned bank=active_bank^1;
        REG16(0x0400000e)=((MAP_ROOM+bank)<<8)|(bank<<2)|overlay_priority;
        display|=0x800;
        overlay_tiles=0;
    }
    /* Until the first room is ready only the frame and sprites are visible. */
    REG16(0x04000000) = display;
}

void re2_video_ui_sprite(unsigned slot, int x, int y, unsigned first,
                         unsigned shape, unsigned size, unsigned flags)
{
    re2_video_sprite(slot,x,y,first,shape,size,flags);
    if (slot<128 && objects[slot][0]!=0x200) {
        objects[slot][2] &= ~(3u<<10);
        objects_dirty=1;
    }
}
