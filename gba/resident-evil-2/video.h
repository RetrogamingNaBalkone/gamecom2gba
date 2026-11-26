#ifndef RE2_VIDEO_H
#define RE2_VIDEO_H

#include <stdint.h>

/* Palette and tile data are halfword-aligned, with Tiles.Write's 4bpp layout.
 * Bezel is the 240x160 result of Bezel.Draw; only the outer columns are used. */
void re2_video_init(const uint16_t palette[16], const uint8_t *bezel_4bpp);

/* A room uses up to 512 characters and 25x20 text-BG map entries in row
 * order. The next present atomically selects the prepared room. */
int re2_video_room(const uint8_t *tiles, unsigned count, const uint16_t *map);
/* Optional foreground for the pending room. Zero pixels expose sprites.
 * Accepts up to 292 unique tiles; the next room clears this layer. */
int re2_video_front(const uint8_t *tiles, unsigned count, const uint16_t *map);
int re2_video_plane(const uint8_t *atlas,const uint8_t *plane,unsigned foreground);
void re2_video_overlay(const uint8_t *tiles,const uint16_t *map,unsigned behind_actors);

/* Queue 4bpp OBJ tiles for the next VBlank. The source remains valid through
 * present. Returns zero on overflow (1024 OBJ tiles, 768 changed per frame,
 * up to 64 transfers). Tiles in a metasprite use GBA 1D mapping. */
int re2_video_obj_tiles(unsigned first, const uint8_t *tiles, unsigned count);

/* Coordinates are relative to the 200x160 LCD. Shape and size use GBA OAM
 * encodings. flags uses bit 0 for horizontal flip and bit 1 for vertical. */
void re2_video_sprite(unsigned slot, int x, int y, unsigned first,
                      unsigned shape, unsigned size, unsigned flags);
void re2_video_ui_sprite(unsigned slot, int x, int y, unsigned first,
                         unsigned shape, unsigned size, unsigned flags);
void re2_video_hide(unsigned slot);
void re2_video_present(void);
void re2_video_lcd_palette(unsigned mapping);

#endif
