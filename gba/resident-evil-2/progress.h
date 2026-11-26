#ifndef RE2_PROGRESS_H
#define RE2_PROGRESS_H
#include <stdint.h>
/* Original locations identify campaign flags used by room scripts. */
extern uint8_t re2_progress[0x29c];
void re2_progress_new(void);
void re2_progress_time(unsigned timer_ticks, unsigned period);
int re2_save_available(void);
int re2_save_load(void);
void re2_save_commit(void);
void re2_save_finish(void);
#endif
