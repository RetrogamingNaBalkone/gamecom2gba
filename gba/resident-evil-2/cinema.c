#include "cinema.h"
#include "audio.h"
#include "video.h"

typedef struct { uint16_t start; uint8_t count, delay; } Scene;
typedef struct { uint16_t start, count; } List;
typedef struct { uint16_t id; uint8_t x, y; } Placement;
typedef struct { uint32_t offset; uint16_t width, height; } Piece;
static const Scene *scenes;
static const List *lists;
static const Placement *placements;
static const Piece *pieces;
static const uint16_t *slots;
static const uint32_t *tile_data;
static unsigned scene_index, next_slot, last_list;
static uint16_t map[500];
static uint32_t scene_tiles[500 * 8];

void re2_cinema_init(const uint8_t *catalog)
{
    const uint32_t *dirs = (const uint32_t *)catalog;
    scenes = (const Scene *)(catalog + dirs[1]);
    slots = (const uint16_t *)(catalog + dirs[2]);
    lists = (const List *)(catalog + dirs[3]);
    placements = (const Placement *)(catalog + dirs[4]);
    pieces = (const Piece *)(catalog + dirs[5]);
    tile_data = (const uint32_t *)(catalog + dirs[6]);
    for (unsigned i=0; i<500; ++i) map[i] = i;
}

void re2_cinema_start(unsigned scene)
{
    scene_index = scene;
    next_slot = 1;  /* SM8500 increments CD before dispatch. */
    last_list = 0xffff;
}

static void draw_piece(const Placement *where)
{
    const Piece *p = &pieces[where->id];
    unsigned columns = (p->width + 7) >> 3;
    const uint32_t *pixels = tile_data + (p->offset >> 2);
    unsigned shift = (where->x & 7) * 4;
    for (unsigned y=0; y<p->height; ++y) {
        unsigned v = where->y + y;
        uint32_t *dst = scene_tiles + (v >> 3) * 200 + (where->x >> 3) * 8 + (v & 7);
        const uint32_t *src = pixels + (y >> 3)*columns*8 + (y & 7);
        for (unsigned x=0; x<columns; ++x, dst+=8, src+=8) {
            uint32_t bits = *src;
            /* Each nonzero palette index supplies an opaque nibble. */
            uint32_t ones = (bits | (bits >> 1) | (bits >> 2)) & 0x11111111u;
            uint32_t mask = ones * 15;
            dst[0] = (dst[0] & ~(mask << shift)) | (bits << shift);
            if (shift && (mask >> (32-shift)))
                dst[8] = (dst[8] & ~(mask >> (32-shift))) | (bits >> (32-shift));
        }
    }
}

void re2_cinema_slot(unsigned slot)
{
    const Scene *s = &scenes[scene_index];
    if (slot >= s->count || slot < next_slot) return;
    unsigned list = last_list;
    while (next_slot <= slot) {
        unsigned code = slots[s->start + next_slot++];
        if (code >= 0xff00) re2_sound(code & 255);
        else list = code;
    }
    if (list == last_list) return;
    last_list = list;
    unsigned overlay=scene_index==0x1a || (scene_index==0x19 && slot<=3);
    for (unsigned i=0; i<4000; ++i) scene_tiles[i]=overlay ? 0 : 0x44444444;
    const List *l = &lists[list];
    for (unsigned i=0; i<l->count; ++i) draw_piece(&placements[l->start+i]);
    if (overlay) re2_video_overlay((const uint8_t *)scene_tiles,map,scene_index==0x1a);
    else re2_video_room((const uint8_t *)scene_tiles,500,map);
}

unsigned re2_cinema_delay(void)
{
    return scenes[scene_index].delay;
}

unsigned re2_cinema_position(void)
{
    return next_slot-1;
}

int re2_cinema_advance(void)
{
    const Scene *s = &scenes[scene_index];
    unsigned i = next_slot;
    while (i < s->count && slots[s->start+i] >= 0xff00) ++i;
    if (i>=s->count) {
        if (i>next_slot) re2_cinema_slot(i-1);
        return 0;
    }
    re2_cinema_slot(i);
    return 1;
}
