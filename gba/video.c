/* Native GBA presentation of game.com blits.
 *
 * Each of the two game.com VRAM pages owns a 25x20-tile 4bpp tile set in BG
 * charblock 0 or 1; flipping the LCDC page only changes BG0's character base.
 * Opaque blits that do not cover a sprite are drawn into those tiles. Every
 * other blit becomes a display item drawn with hardware OBJs in blit order.
 * An item disappears once later opaque blits have covered its whole area. */
#include "gamecom.h"
#include "video.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define EWRAM __attribute__((section(".ewram")))
#define IWRAM_CODE __attribute__((section(".iwram.code"), target("arm"), noinline))

enum { VRAM = 0x06000000, OBJ_VRAM = 0x06010000, OAM = 0x07000000, MAP_GAME = 31, MAP_BEZEL = 30 };
enum { CELLS_X = 50, CELLS_Y = 40, MAX_ITEMS = 96, MAX_CACHE = 160, OBJ_TILES = 1024 };

typedef struct {
    uint32_t seq;
    int16_t x, y;              /* screen position of the top-left pixel */
    uint8_t w, h;              /* screen width (columns) and height (rows) */
    uint8_t cache;
} item_t;

typedef struct {
    const uint8_t *source;
    uint8_t sx, sy, w, h, pal, dmc;
    uint16_t tile, tiles;
    uint8_t refs, used;
} cache_t;

static item_t items[2][MAX_ITEMS] EWRAM;
static uint8_t item_count[2] EWRAM;
static uint32_t covered[2][CELLS_Y][CELLS_X] EWRAM;
static cache_t cache[MAX_CACHE] EWRAM;
static uint32_t tile_used[OBJ_TILES / 32] EWRAM;
static uint16_t oam[128][4] EWRAM;
static uint32_t sequence;
static uint8_t lcd_mode = 0xff;
static uint16_t shades[5];
static uint8_t pixels[64 * 64] EWRAM;

static const uint8_t mode_shade[4][4] = { {4,3,2,0}, {4,3,1,0}, {4,3,1,0}, {4,2,1,0} };

static void copy16(const void *source, uintptr_t destination, unsigned halfwords) {
    *(volatile uintptr_t *)0x040000d4 = (uintptr_t)source;
    *(volatile uintptr_t *)0x040000d8 = destination;
    *(volatile uint32_t *)0x040000dc = 0x80000000u | halfwords;
}

void video_init(const uint16_t palette[16], const uint8_t *bezel_4bpp) {
    REG16(0x04000000) = 0x80;
    for (int i = 0; i < 5; ++i) shades[i] = palette[8 + i];
    copy16(palette, 0x05000000, 16);
    copy16(palette, 0x05000200, 16);
    volatile uint16_t *game = (volatile uint16_t *)(VRAM + MAP_GAME * 2048);
    volatile uint16_t *bezel = (volatile uint16_t *)(VRAM + MAP_BEZEL * 2048);
    for (int i = 0; i < 1024; ++i) game[i] = bezel[i] = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 25; ++x) game[32 * y + x] = (uint16_t)(y * 25 + x);
    for (int y = 0; y < 20; ++y)
        for (int side = 0; side < 2; ++side) {
            int column = side * 27, index = y * 6 + side * 3;
            copy16(bezel_4bpp + (30 * y + column) * 32, VRAM + 0x8000 + index * 32, 48);
            for (int i = 0; i < 3; ++i) bezel[32 * y + column + i] = (uint16_t)(index + i);
        }
    for (unsigned i = 0; i < 2 * 16384 / 2; ++i) ((volatile uint16_t *)VRAM)[i] = 0x1111;
    REG16(0x04000008) = (MAP_GAME << 8) | (0 << 2);          /* BG0: game page */
    REG16(0x0400000a) = (MAP_BEZEL << 8) | (2 << 2) | 1;     /* BG1: bezel */
    REG16(0x04000010) = 512 - 20; REG16(0x04000012) = 0;
    REG16(0x04000014) = 0; REG16(0x04000016) = 0;
    REG16(0x04000040) = (20 << 8) | 220;
    REG16(0x04000044) = 160;
    REG16(0x04000048) = 0x11;   /* BG0 and OBJ inside the LCD */
    REG16(0x0400004a) = 0x02;   /* bezel outside */
    for (int i = 0; i < 128; ++i) { oam[i][0] = 0x200; oam[i][1] = oam[i][2] = oam[i][3] = 0; }
    copy16(oam, OAM, 512);
    REG16(0x04000000) = 0x0000 | 0x0040 | 0x0100 | 0x0200 | 0x1000 | 0x2000;
}

/* ---- BG pixels ---- */

static inline void bg_plot(int page, int x, int y, int color) {
    uintptr_t at = VRAM + page * 0x4000 + (uintptr_t)(((y >> 3) * 25 + (x >> 3)) * 32 + (y & 7) * 4 + ((x & 7) >> 1));
    volatile uint16_t *half = (volatile uint16_t *)(at & ~1u);
    int shift = ((at & 1) << 3) + ((x & 1) << 2);
    *half = (uint16_t)((*half & ~(0xf << shift)) | (color << shift));
}

uint32_t video_vram_writes, video_blits;

IWRAM_CODE void video_vram_write(uint16_t offset, uint8_t value) {
    video_vram_writes++;
    int page = offset >> 13, within = offset & 0x1fff;
    int stride = (gc_ram[0x31] & 0x20) ? 50 : 40;
    int x = within / stride, y = (within % stride) * 4;
    if (x >= 200 || y >= 160) return;
    for (int i = 0; i < 4; ++i) bg_plot(page, x, y + i, 1 + ((value >> (6 - 2 * i)) & 3));
}

/* ---- coverage ---- */

static void cover(int page, int x, int y, int w, int h) {
    int cx0 = (x + 3) >> 2, cy0 = (y + 3) >> 2, cx1 = (x + w) >> 2, cy1 = (y + h) >> 2;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > CELLS_X) cx1 = CELLS_X;
    if (cy1 > CELLS_Y) cy1 = CELLS_Y;
    for (int cy = cy0; cy < cy1; ++cy)
        for (int cx = cx0; cx < cx1; ++cx) covered[page][cy][cx] = sequence;
}

static int buried(int page, const item_t *it) {
    int cx0 = it->x >> 2, cy0 = it->y >> 2, cx1 = (it->x + it->w + 3) >> 2, cy1 = (it->y + it->h + 3) >> 2;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > CELLS_X) cx1 = CELLS_X;
    if (cy1 > CELLS_Y) cy1 = CELLS_Y;
    for (int cy = cy0; cy < cy1; ++cy)
        for (int cx = cx0; cx < cx1; ++cx)
            if (covered[page][cy][cx] <= it->seq) return 0;
    return 1;
}

static void release(int index) {
    if (cache[index].refs) cache[index].refs--;
}

static void prune(int page) {
    int n = 0;
    for (int i = 0; i < item_count[page]; ++i) {
        if (buried(page, &items[page][i])) { release(items[page][i].cache); continue; }
        items[page][n++] = items[page][i];
    }
    item_count[page] = (uint8_t)n;
}

static int overlaps_items(int page, int x, int y, int w, int h) {
    for (int i = 0; i < item_count[page]; ++i) {
        const item_t *it = &items[page][i];
        if (it->x < x + w && x < it->x + it->w && it->y < y + h && y < it->y + it->h) return 1;
    }
    return 0;
}

/* ---- source pixels ---- */

typedef struct {
    const uint8_t *data;
    uint16_t mask;
    int width;
    int sx, sy, step_x, step_y;
} source_t;

static inline int source_pixel(const source_t *s, int along, int across) {
    int x = s->sx + along * s->step_x, y = s->sy + across * s->step_y;
    uint8_t byte = s->data[(uint16_t)(s->width * y + (x >> 2)) & s->mask];
    return (byte >> ((3 - (x & 3)) * 2)) & 3;
}

/* ---- OBJ tile cache ---- */

static int alloc_tiles(int count) {
    for (int start = 0; start + count <= OBJ_TILES; ) {
        int run = 0;
        while (run < count && !(tile_used[(start + run) >> 5] & (1u << ((start + run) & 31)))) run++;
        if (run == count) {
            for (int i = 0; i < count; ++i) tile_used[(start + i) >> 5] |= 1u << ((start + i) & 31);
            return start;
        }
        start += run + 1;
    }
    return -1;
}

static void free_entry(int index) {
    cache_t *c = &cache[index];
    for (int i = 0; i < c->tiles; ++i) tile_used[(c->tile + i) >> 5] &= ~(1u << ((c->tile + i) & 31));
    c->used = 0;
}

/* Smallest GBA OBJ shape that covers a piece of at most 64x64 pixels. */
static const uint8_t obj_shapes[12][2] = { {8,8}, {16,8}, {8,16}, {16,16}, {32,8}, {8,32}, {32,16},
                                           {16,32}, {32,32}, {64,32}, {32,64}, {64,64} };
static void fit_shape(int w, int h, int *pw, int *ph) {
    for (int i = 0; i < 12; ++i)
        if (obj_shapes[i][0] >= w && obj_shapes[i][1] >= h) { *pw = obj_shapes[i][0]; *ph = obj_shapes[i][1]; return; }
    *pw = *ph = 64;
}

/* Tiles are laid out piece by piece; pieces are at most 64x64 and follow GBA shapes. */
static int piece_tiles(int w, int h) {
    int total = 0;
    for (int py = 0; py < h; py += 64)
        for (int px = 0; px < w; px += 64) {
            int pw, ph;
            fit_shape(w - px < 64 ? w - px : 64, h - py < 64 ? h - py : 64, &pw, &ph);
            total += (pw / 8) * (ph / 8);
        }
    return total;
}

IWRAM_CODE static void render_piece(const source_t *src, int transparent, uint8_t pal,
                                    int ox, int oy, int pw, int ph, int w, int h, uint16_t tile) {
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x) {
            int gx = ox + x, gy = oy + y, color = 0;
            if (gx < w && gy < h) {
                /* screen column gx is source y (across), screen row gy is source x (along) */
                int v = source_pixel(src, gy, gx);
                if (v || !transparent) color = 1 + ((pal >> (v * 2)) & 3);
            }
            pixels[y * 64 + x] = (uint8_t)color;
        }
    volatile uint16_t *out = (volatile uint16_t *)(OBJ_VRAM + tile * 32);
    for (int ty = 0; ty < ph / 8; ++ty)
        for (int tx = 0; tx < pw / 8; ++tx)
            for (int row = 0; row < 8; ++row) {
                const uint8_t *p = pixels + (ty * 8 + row) * 64 + tx * 8;
                *out++ = (uint16_t)(p[0] | p[1] << 4 | p[2] << 8 | p[3] << 12);
                *out++ = (uint16_t)(p[4] | p[5] << 4 | p[6] << 8 | p[7] << 12);
            }
}

static int cached_tiles(const source_t *src, const gc_blit_t *b, int w, int h) {
    for (int i = 0; i < MAX_CACHE; ++i) {
        cache_t *c = &cache[i];
        if (c->used && c->source == src->data && c->sx == b->sx && c->sy == b->sy && c->w == b->w &&
            c->h == b->h && c->pal == b->pal && c->dmc == (b->dmc & 0x19)) {
            c->refs++;
            return i;
        }
    }
    int count = piece_tiles(w, h), tile = alloc_tiles(count);
    if (tile < 0) {
        for (int i = 0; i < MAX_CACHE; ++i) if (cache[i].used && !cache[i].refs) free_entry(i);
        tile = alloc_tiles(count);
        if (tile < 0) return -1;
    }
    int slot = -1;
    for (int i = 0; i < MAX_CACHE && slot < 0; ++i) if (!cache[i].used) slot = i;
    if (slot < 0) {
        for (int i = 0; i < MAX_CACHE && slot < 0; ++i) if (!cache[i].refs) { free_entry(i); slot = i; }
        if (slot < 0) return -1;
        tile = alloc_tiles(count);
        if (tile < 0) return -1;
    }
    cache_t *c = &cache[slot];
    c->source = src->data; c->sx = b->sx; c->sy = b->sy; c->w = b->w; c->h = b->h;
    c->pal = b->pal; c->dmc = (uint8_t)(b->dmc & 0x19); c->tile = (uint16_t)tile;
    c->tiles = (uint16_t)count; c->refs = 1; c->used = 1;
    for (int py = 0; py < h; py += 64)
        for (int px = 0; px < w; px += 64) {
            int pw, ph;
            fit_shape(w - px < 64 ? w - px : 64, h - py < 64 ? h - py : 64, &pw, &ph);
            render_piece(src, !(b->dmc & 1), b->pal, px, py, pw, ph, w, h, (uint16_t)tile);
            tile += (pw / 8) * (ph / 8);
        }
    return slot;
}

/* ---- blits ---- */

IWRAM_CODE void video_blit(const gc_blit_t *b, const uint8_t *data, uint16_t mask, int width) {
    int page = b->page & 1;
    int rows = b->w + 1, columns = b->h + 1;          /* DMA x runs down the screen */
    int x = b->dy, y = b->dx;
    source_t src = { data, mask, width, b->sx, b->sy, (b->dmc & 8) ? -1 : 1, (b->dmc & 0x10) ? -1 : 1 };
    sequence++;
    video_blits++;
    if ((b->dmc & 1) && !overlaps_items(page, x, y, columns, rows)) {
        for (int c = 0; c < columns; ++c) {
            int sxp = x + c;
            if (sxp >= 200) break;
            for (int r = 0; r < rows; ++r) {
                int syp = y + r;
                if (syp >= 160) break;
                int v = source_pixel(&src, r, c);
                bg_plot(page, sxp, syp, 1 + ((b->pal >> (v * 2)) & 3));
            }
        }
        cover(page, x, y, columns, rows);
        return;
    }
    if (b->dmc & 1) cover(page, x, y, columns, rows);
    if (item_count[page] >= MAX_ITEMS) prune(page);
    if (item_count[page] >= MAX_ITEMS) return;
    int slot = cached_tiles(&src, b, columns, rows);
    if (slot < 0) { prune(page); slot = cached_tiles(&src, b, columns, rows); if (slot < 0) return; }
    item_t *it = &items[page][item_count[page]++];
    it->seq = sequence; it->x = (int16_t)x; it->y = (int16_t)y;
    it->w = (uint8_t)(columns > 255 ? 255 : columns); it->h = (uint8_t)rows; it->cache = (uint8_t)slot;
}

void video_vram_copy(const gc_blit_t *b) {
    int from = b->page & 1, to = (b->page >> 1) & 1;
    int rows = b->w + 1, columns = b->h + 1;
    if (from == to) return;
    sequence++;
    for (int c = 0; c < columns; ++c) {
        int sxp = b->dy + c, sx = b->sy + c;
        if (sxp >= 200 || sx >= 200) break;
        for (int r = 0; r < rows; ++r) {
            int syp = b->dx + r, sy = b->sx + r;
            if (syp >= 160 || sy >= 160) break;
            uintptr_t at = VRAM + from * 0x4000 + (uintptr_t)(((sy >> 3) * 25 + (sx >> 3)) * 32 + (sy & 7) * 4 + ((sx & 7) >> 1));
            uint16_t half = *(volatile uint16_t *)(at & ~1u);
            int shift = ((at & 1) << 3) + ((sx & 1) << 2);
            bg_plot(to, sxp, syp, (half >> shift) & 15);
        }
    }
    cover(to, b->dy, b->dx, columns, rows);
    if (b->dx == 0 && b->dy == 0 && rows >= 160 && columns >= 200) {
        for (int i = 0; i < item_count[to]; ++i) release(items[to][i].cache);
        item_count[to] = 0;
        for (int i = 0; i < item_count[from]; ++i) {
            items[to][i] = items[from][i];
            items[to][i].seq = sequence;
            cache[items[to][i].cache].refs++;
        }
        item_count[to] = item_count[from];
    }
}

/* ---- presentation ---- */

static int shape_bits(int pw, int ph, int *shape, int *size) {
    static const uint8_t sizes[4] = { 8, 16, 32, 64 };
    for (int s = 0; s < 4; ++s) {
        int d = sizes[s];
        if (pw == d && ph == d) { *shape = 0; *size = s; return 1; }
    }
    static const uint8_t wide[4][2] = { {16,8}, {32,8}, {32,16}, {64,32} };
    for (int s = 0; s < 4; ++s) {
        if (pw == wide[s][0] && ph == wide[s][1]) { *shape = 1; *size = s; return 1; }
        if (ph == wide[s][0] && pw == wide[s][1]) { *shape = 2; *size = s; return 1; }
    }
    return 0;
}

void video_present(void) {
    uint8_t lcdc = gc_lcdc();
    int page = (lcdc >> 6) & 1;
    if ((lcdc & 0x30) != lcd_mode) {
        lcd_mode = lcdc & 0x30;
        for (int v = 0; v < 4; ++v) {
            uint16_t color = shades[mode_shade[lcd_mode >> 4][v]];
            ((volatile uint16_t *)0x05000000)[1 + v] = color;
            ((volatile uint16_t *)0x05000200)[1 + v] = color;
        }
    }
    prune(page);
    int n = 0;
    for (int i = item_count[page] - 1; i >= 0 && n < 128; --i) {
        const item_t *it = &items[page][i];
        const cache_t *c = &cache[it->cache];
        int tile = c->tile, w = it->w, h = it->h;
        for (int py = 0; py < h; py += 64)
            for (int px = 0; px < w; px += 64) {
                int pw, ph;
                fit_shape(w - px < 64 ? w - px : 64, h - py < 64 ? h - py : 64, &pw, &ph);
                int shape, size, x = it->x + px + 20, y = it->y + py;
                if (n < 128 && shape_bits(pw, ph, &shape, &size) && y < 160 && x < 240) {
                    oam[n][0] = (uint16_t)((y & 0xff) | shape << 14);
                    oam[n][1] = (uint16_t)((x & 0x1ff) | size << 14);
                    oam[n][2] = (uint16_t)tile;
                    n++;
                }
                tile += (pw / 8) * (ph / 8);
            }
    }
    for (int i = n; i < 128; ++i) oam[i][0] = 0x200;
    while (REG16(0x04000006) < 160) {}
    copy16(oam, OAM, 512);
    REG16(0x04000008) = (uint16_t)((MAP_GAME << 8) | (page << 2));
    REG16(0x04000000) = (uint16_t)((lcdc & 0x80 ? 0x0100 : 0) | 0x0040 | 0x0200 | 0x1000 | 0x2000);
}
