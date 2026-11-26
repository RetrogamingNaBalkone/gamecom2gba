#ifndef RE2_CAMPAIGN_H
#define RE2_CAMPAIGN_H
#include <stdint.h>

void re2_campaign_init(const uint8_t *assets);
int re2_project_x(unsigned area, unsigned camera, int x, unsigned from_depth, unsigned to_depth);
/* Direction is the cartridge facing nibble: 0 back, 2 right, 4 front, 6 left.
   Returns zero for an unobstructed step, one for a wall, two after a transition. */
int re2_campaign_move(unsigned direction, int dx, int dy);
/* Returns one after an interaction. Scene requests use the cartridge RAM flags. */
int re2_campaign_action(void);
int re2_pickup_pending(void);
void re2_pickup_confirm(int accept);
void re2_box_exchange(unsigned carried, unsigned box);
/* Visible ground pickups use one 32x32 native OBJ each. The index is 0 or 1. */
const uint8_t *re2_ground_item(unsigned index,int *x,int *y);
/* Puzzle props are split into at most four native 64x64 objects. */
const uint8_t *re2_world_prop(unsigned part,int *x,int *y);
int re2_push_start(void);
int re2_push_step(unsigned frame);
#endif
