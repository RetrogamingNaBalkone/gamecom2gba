#ifndef RE2_SPECIAL_H
#define RE2_SPECIAL_H
#include "actor.h"
#include <stdint.h>
void re2_switch_init(const uint8_t *tiles);
int re2_switch_press(void);
const uint8_t *re2_switch_picture(unsigned index,int *x,int *y);
int re2_final_ai(struct Re2Actor *boss, struct Re2Actor *player);
void re2_final_sense(struct Re2Actor *boss,const struct Re2Actor *player);
void re2_final_hit(struct Re2Actor *boss, struct Re2Actor *player);
#endif
