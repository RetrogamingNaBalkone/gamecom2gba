/* Starts Indy 500 from the state the BIOS establishes at its request-0 call. */
#include "../gamecom.h"
#include "../sm8500.h"
#include "../pcm_isr.h"
#include "launch.h"
#include "entry_state.h"

/* The kernel PCM handler at 2:357F and Indy 500's copy at 80:5C7E share the kernel table at 36D4. */
static const pcm_isr_t indy_pcm[] = {
    { 0x357f, 0, 0x36d4, 0x35dc, 0x35fa, 1, 0 },
    { 0x5c7e, 0x80, 0x36d4, 0x5cdd, 0x5cfb, 0, 1 },
    { 0 } };

void indy_launch(int native_handlers) {
    /* bit 0: PCM fast path, bit 1: idle hints */
    for (unsigned i = 0; i < sizeof entry_ram / sizeof entry_ram[0]; ++i) {
        uint16_t a = entry_ram[i].address;
        if (a >= 0xe000) gc_ext[a - 0xe000] = entry_ram[i].value;
        else gc_ram[a] = entry_ram[i].value;
    }
    sm8500_reset_state();
    for (unsigned i = 0; i < sizeof entry_registers / sizeof entry_registers[0]; ++i)
        cpu.reg[entry_registers[i].address] = entry_registers[i].value;
    cpu.pc = ENTRY_PC;
    cpu.sp = ENTRY_SP;
    cpu.ps0 = ENTRY_PS0;
    cpu.ps1 = ENTRY_PS1;
    cpu.sys = ENTRY_SYS;
    for (uint16_t a = 0x25; a <= 0x28; ++a) gc_write(a, gc_ram[a]);
    static const uint8_t reloads[2] = { ENTRY_TIMER0_RELOAD, ENTRY_TIMER1_RELOAD };
    static const int counts[2] = { ENTRY_TIMER0_COUNT, ENTRY_TIMER1_COUNT };
    gc_restore_timing(ENTRY_CYCLE, ENTRY_CLOCK, reloads, counts, ENTRY_DMA_BUSY, ENTRY_PENDING);
    gc_pcm_isr = (native_handlers & 1) ? indy_pcm : 0;
    gc_idle_hints = (native_handlers & 2) != 0;
}
