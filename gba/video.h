/* Native GBA presentation of game.com blits (see video.c). */
#ifndef VIDEO_H
#define VIDEO_H
#include <stdint.h>

/* palette: 0 backdrop, 5-6 bezel, 8-12 the five LCD shades from black to the lightest green. */
void video_init(const uint16_t palette[16], const uint8_t *bezel_4bpp);
/* Waits for VBlank and shows the game.com page selected by LCDC. */
void video_present(void);

#endif
