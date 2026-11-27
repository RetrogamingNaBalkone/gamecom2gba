/* SM8500 (game.com SM8521) CPU state shared by the interpreter and recompiled code. */
#ifndef SM8500_H
#define SM8500_H
#include <stdint.h>

#define SM_FC 0x80
#define SM_FZ 0x40
#define SM_FS 0x20
#define SM_FV 0x10
#define SM_FD 0x08
#define SM_FH 0x04
#define SM_FB 0x02
#define SM_FI 0x01

typedef struct {
    uint16_t pc, sp;
    uint8_t ps0, ps1, sys;
    uint8_t halted, stopped, trapped;
    uint16_t trap_pc;
    uint16_t pending;        /* interrupt request lines, bit = line */
    int32_t cycles;          /* emulated cycles left in the current slice */
    uint8_t reg[0x108];      /* banked register file, bank base = PS0 & F8 */
} sm8500_t;

extern sm8500_t cpu;

/* Interrupt lines in priority order (lower = higher priority). */
enum { SM_IRQ_DMA, SM_IRQ_TIM0, SM_IRQ_EXT, SM_IRQ_UART, SM_IRQ_LCDC, SM_IRQ_TIM1,
       SM_IRQ_CK, SM_IRQ_PIO, SM_IRQ_LINES };

/* Bus provided by gamecom.c. */
uint8_t gc_read(uint16_t address);
void gc_write(uint16_t address, uint8_t value);

void sm8500_reset_state(void);
void sm8500_raise(int line);
/* Pushes the return address and PS1 as interrupt entry does, and disables interrupts. */
void sm8500_push_interrupt(void);
/* Runs until the cycle budget is exhausted, the CPU halts or traps. */
void sm8500_run(void);

#endif
