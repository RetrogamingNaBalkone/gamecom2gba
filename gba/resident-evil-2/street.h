#ifndef RE2_STREET_H
#define RE2_STREET_H
#include <stdint.h>
void re2_street_enter(const uint8_t *assets);
void re2_street_resume(void);
void re2_street_tick(unsigned held, unsigned pressed);
#endif
