/* Native fast path for the Tiger SDK PCM interrupt handler (see pcm_isr.c). */
#ifndef PCM_ISR_H
#define PCM_ISR_H
#include <stdint.h>

typedef struct {
    uint16_t entry;     /* handler address stored at RAM 0102 */
    uint8_t page;       /* page mapped at 4000 from RAM A6 */
    uint16_t table;     /* kernel difference table */
    uint16_t finish;    /* instruction that handles the end of a sample */
    uint16_t tick;      /* 200 Hz system tick inside the handler */
    uint8_t any_page;   /* handler lives in the kernel window, A6 does not matter */
    uint8_t clears_b1;  /* handler starts with BCLR B1,#5 */
} pcm_isr_t;

/* Kernel handler 2:357F followed by any cartridge handlers, ended by entry 0. */
extern const pcm_isr_t *gc_pcm_isr;
int gc_pcm_interrupt(void);

#endif
