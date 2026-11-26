/* Test host. The installed Tigerbyte CPU executes the original interaction
 * handler in its cartridge banks, with each case supplying the game state. */
#include <stdint.h>
#include <string.h>
#include "cpu/sm8521.h"
#include "progress.h"
#include "puzzle.h"

static uint8_t memory[65536];
static uint8_t read8(void *ctx,uint16_t a) { (void)ctx; return memory[a]; }
static void write8(void *ctx,uint16_t a,uint8_t v) { (void)ctx; memory[a]=v; }

void load_banks(const uint8_t *rom)
{
    memcpy(memory+0x4000,rom+0x40000,8192);
    memcpy(memory+0x6000,rom+0x48000,8192);
    memory[0x8000]=0x49; memory[0x8001]=0x69; memory[0x8002]=0x4d;
}

int compare_puzzle(const uint8_t *state)
{
    sm8521_t cpu;
    memcpy(memory,state,0x29c);
    memcpy(re2_progress,state,0x29c);
    sm8521_init(&cpu,read8,write8,0);
    cpu.pc=0x8000;
    memory[0x19]=0x40;
    memory[0x1c]=7;
    memory[0x1d]=0xf0;
    cpu.sp=0x7f0;
    memory[0xbf]=0x8b;
    for (unsigned step=0;cpu.pc!=0x8003;++step) {
        if (step>400 || cpu.trapped) return -1;
        sm8521_step(&cpu);
    }
    re2_puzzle_action();
    for (unsigned i=0xc0;i<0x29c;++i)
        if (i!=0xcd && re2_progress[i]!=memory[i]) return i;
    return 0;
}

