#ifndef RE2_AUDIO_H
#define RE2_AUDIO_H
#include <stdint.h>
void re2_audio_init(const uint8_t *assets);
void re2_audio_tick(void);
int re2_audio_active(void);
void re2_sound(unsigned id);
void re2_sound_restart(unsigned id);
#endif
