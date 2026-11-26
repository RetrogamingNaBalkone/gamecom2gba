#ifndef RE2_CINEMA_H
#define RE2_CINEMA_H
#include <stdint.h>

void re2_cinema_init(const uint8_t *catalog);
void re2_cinema_start(unsigned scene);
/* Slot indexes include inline audio cues. */
unsigned re2_cinema_delay(void);
unsigned re2_cinema_position(void);
int re2_cinema_advance(void);
void re2_cinema_slot(unsigned slot);
#endif
