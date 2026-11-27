/* Native fast path for the Tiger SDK PCM interrupt handler.
 *
 * While a sample plays, timer 1 interrupts at 4.9152 MHz / 208. The kernel wrapper
 * (2045 -> 2133) saves PS0 in A4, selects register bank 38, keeps MMU2 in r14, maps
 * the page from A6 and jumps through 0102 to the cartridge handler. That handler
 * writes one DAC sample every third interrupt, decodes the next 4-bit difference and
 * every 40 samples runs the 200 Hz system tick. This file performs the frequent
 * steps natively with the same register and RAM effects. The tick and the end of a
 * sample enter the original handler at the matching instruction. */
#include "gamecom.h"
#include "sm8500.h"
#include "pcm_isr.h"

#define IWRAM_CODE __attribute__((section(".iwram.code"), target("arm"), noinline))

const pcm_isr_t *gc_pcm_isr;

static inline uint8_t *bank38(int r) { return &cpu.reg[0x38 + r]; }

static void enter_handler(uint16_t pc) {
    /* Interrupt entry: return address and PS1 on the stack, then the kernel wrapper. */
    sm8500_push_interrupt();
    gc_write(0xa4, cpu.ps0);
    cpu.ps0 = 0x38;
    *bank38(14) = gc_read(0x26);
    gc_write(0x26, gc_read(0xa6));
    cpu.pc = pc;
}

/* Returns nonzero when the timer 1 interrupt has been handled or entered. */
IWRAM_CODE int gc_pcm_interrupt(void) {
    const pcm_isr_t *isr = gc_pcm_isr;
    uint16_t target = (uint16_t)(gc_ram[0x102] << 8 | gc_ram[0x103]);
    if (!isr) return 0;
    while (isr->entry && (isr->entry != target || (!isr->any_page && gc_ram[0xa6] != isr->page))) isr++;
    if (!isr->entry) return 0;
    /* Wrapper side effects that persist after the handler returns. */
    gc_ram[0xa4] = cpu.ps0;
    *bank38(14) = gc_ram[0x26];
    if (isr->clears_b1) gc_ram[0xb1] &= (uint8_t)~0x20;
    if (--gc_ram[0xac]) return 1;
    gc_ram[0xac] = 3;
    uint8_t saved_mmu3 = gc_ram[0x27];
    *bank38(15) = saved_mmu3;
    gc_write(0x27, *bank38(4));
    gc_write(0x4e, gc_ram[0xb0]);
    uint16_t rr2 = (uint16_t)(*bank38(2) << 8 | *bank38(3));
    uint8_t r0;
    if (gc_ram[0xa8] & 4) {
        gc_ram[0xa8] &= (uint8_t)~4;
        r0 = gc_read(rr2++);
        if (rr2 & 0x8000) { (*bank38(4))++; rr2 = (uint16_t)(0x6000 | (rr2 & 0xff)); }
        r0 = (uint8_t)(r0 >> 4 | r0 << 4);
    } else {
        gc_ram[0xa8] |= 4;
        r0 = gc_read(rr2);
    }
    *bank38(2) = (uint8_t)(rr2 >> 8);
    *bank38(3) = (uint8_t)rr2;
    int negative = r0 & 1;
    r0 = (uint8_t)(((r0 >> 1) & 7) | *bank38(1));
    *bank38(13) = (uint8_t)(r0 << 1);
    *bank38(12) = 0;
    uint16_t entry = (uint16_t)(isr->table + (uint8_t)(r0 << 1));   /* SLL r13 is an 8-bit shift */
    uint8_t delta = gc_read(entry), low = gc_read((uint16_t)(entry + 1));
    *bank38(0) = delta;
    *bank38(1) = low;
    if (negative) gc_ram[0xb0] = gc_ram[0xb0] < delta ? 0 : (uint8_t)(gc_ram[0xb0] - delta);
    else gc_ram[0xb0] = gc_ram[0xb0] + delta > 0xff ? 0xff : (uint8_t)(gc_ram[0xb0] + delta);
    gc_write(0x27, *bank38(15));
    if (*bank38(4) == *bank38(8)) {
        uint16_t rr6 = (uint16_t)(*bank38(6) << 8 | *bank38(7));
        if (rr2 >= rr6) { enter_handler(isr->finish); return 1; }
    }
    if (--gc_ram[0xad]) return 1;
    gc_ram[0xad] = 0x28;
    enter_handler(isr->tick);
    return 1;
}
