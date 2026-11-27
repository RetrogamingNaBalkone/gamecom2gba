/* game.com machine model: memory map, I/O registers, timers, key matrix and DMA dispatch.
 * Register behaviour follows MAME's gamecom driver and the Tigerbyte reference emulator. */
#include "gamecom.h"
#include "sm8500.h"

#define IWRAM_CODE __attribute__((section(".iwram.code"), target("arm"), noinline))

__attribute__((section(".iwram.data"))) uint8_t gc_ram[0x1000];
uint8_t gc_ext[0x2000];
gc_media_t gc_media;

__attribute__((section(".iwram.data"))) static const uint8_t *window[4];          /* 2000, 4000, 6000, 8000 */
static uint8_t in0 = 0xff, in1 = 0xff, in2 = 0xff;
static uint16_t touch[13];
static int dma_armed, dma_age, dma_busy;
static int timer_count[2];
static uint8_t reload[2];
static uint32_t clock_accum;
static int uart_left;
static const uint8_t unmapped[0x2000];

static const int timer_divisor[8] = { 1, 1024, 2048, 4096, 8192, 16384, 32768, 65536 };

/* Internal ROM interrupt vectors: each line jumps to its kernel entry at 2039 + 3n. */
uint16_t gc_vector(uint8_t index) {
    switch (index) {
    case 0x02: return 0x2039;   /* timer 0 */
    case 0x06: return 0x203c;   /* external */
    case 0x08: return 0x203f;   /* UART */
    case 0x0e: return 0x2042;   /* LCDC */
    case 0x12: return 0x2045;   /* timer 1 */
    case 0x16: return 0x2048;   /* clock */
    case 0x1a: return 0x204b;   /* PIO */
    default: return 0x1085;     /* IRET */
    }
}

static void map_window(int w) {
    uint8_t page = gc_ram[0x25 + w];
    if (page < 0x20) window[w] = gc_media.bios + ((uint32_t)page << 13);
    else window[w] = gc_media.cart + (((uint32_t)page << 13) & gc_media.cart_mask);
}

static void scan_keys(void) {
    uint16_t mux = (uint16_t)(gc_ram[0x15] | gc_ram[0x16] << 8);
    int column = -1;
    static const uint16_t columns[13] = { 0xfffb, 0xfff7, 0xffef, 0xffdf, 0xffbf, 0xff7f, 0xfeff,
                                          0xfdff, 0xfbff, 0xf7ff, 0xefff, 0xdfff, 0xbfff };
    if (mux == 0x7fff) {
        gc_ram[0x14] = in0;
        gc_ram[0x15] = (uint8_t)((gc_ram[0x15] & 0xfc) | (in1 & 3));
        return;
    }
    if (mux == 0xffff) {
        gc_ram[0x14] = (uint8_t)((gc_ram[0x14] & 0xfc) | (in2 & 3));
        gc_ram[0x15] = 0xff;
        return;
    }
    for (int i = 0; i < 13; ++i) if (columns[i] == mux) column = i;
    if (column < 0) return;
    if (touch[column]) {
        uint16_t rows = (uint16_t)(touch[column] ^ 0x3ff);
        gc_ram[0x14] = (uint8_t)rows;
        gc_ram[0x15] = (uint8_t)((gc_ram[0x15] & 0xfc) | (rows >> 8));
    } else {
        gc_ram[0x14] = 0xff;
        gc_ram[0x15] |= 3;
    }
}

static void run_dma(void) {
    gc_blit_t b;
    dma_armed = 0;
    b.dmc = gc_ram[0x34]; b.sx = gc_ram[0x35]; b.sy = gc_ram[0x36]; b.w = gc_ram[0x37];
    b.h = gc_ram[0x38]; b.dx = gc_ram[0x39]; b.dy = gc_ram[0x3a]; b.pal = gc_ram[0x3b];
    b.bank = gc_ram[0x3c]; b.page = gc_ram[0x3d]; b.lch = gc_ram[0x31];
    switch (b.dmc & 6) {
    case 0: video_vram_copy(&b); break;
    case 2:
        if (b.bank < 16) video_blit(&b, gc_media.bios + ((uint32_t)b.bank << 14), 0x3fff, 64);
        else video_blit(&b, gc_media.cart + (((uint32_t)b.bank << 14) & gc_media.cart_mask), 0x3fff, 64);
        break;
    case 4: video_blit(&b, gc_ext, 0x1fff, 64); break;
    default: break;   /* VRAM to extended RAM is not used by supported cartridges */
    }
    int pixels = (b.w + 1) * (b.h + 1);
    dma_busy = pixels + (pixels >> 1);
    if (dma_busy < 8) dma_busy = 8;
}

void gc_set_input(uint8_t a, uint8_t b, uint8_t c) { in0 = a; in1 = b; in2 = c; }
void gc_set_touch(int column, uint16_t rows) { if (column >= 0 && column < 13) touch[column] = rows; }
uint8_t gc_lcdc(void) { return gc_ram[0x30]; }

/* Loops that only poll state changed by interrupt handlers. Taking their backward
 * branch is equivalent to halting until the next interrupt and rechecking. */
int gc_idle_loop(uint16_t branch, uint16_t target) {
    /* BIOS page 5, event wait while a PCM sample plays: 43F3 BBS A8,#0,43CE. */
    if (branch == 0x43f3 && target == 0x43ce && gc_ram[0x26] == 0x05) return 1;
    return 0;
}

IWRAM_CODE uint8_t gc_read(uint16_t a) {
    if (a < 0x1000) {
        switch (a) {
        case 0x19: return cpu.sys;
        case 0x1c: return (uint8_t)(cpu.sp >> 8);
        case 0x1d: return (uint8_t)cpu.sp;
        case 0x1e: return cpu.ps0;
        case 0x1f: return cpu.ps1;
        }
        return gc_ram[a];
    }
    if (a < 0x2000) {
        if (a < 0x1020) {
            static const uint8_t lines[16] = { 0, 0x02, 0, 0x06, 0x08, 0, 0, 0x0e, 0, 0x12, 0, 0x16, 0, 0x1a, 0, 0 };
            uint16_t v = gc_vector(lines[(a - 0x1000) >> 1]);
            return (a & 1) ? (uint8_t)v : (uint8_t)(v >> 8);
        }
        return a == 0x1085 ? 0xf9 : 0xff;
    }
    if (a < 0xa000) return window[(a - 0x2000) >> 13][a & 0x1fff];
    if (a < 0xe000) return 0;
    return gc_ext[a - 0xe000];
}

IWRAM_CODE void gc_write(uint16_t a, uint8_t v) {
    if (a >= 0xe000) { gc_ext[a - 0xe000] = v; return; }
    if (a >= 0xa000) { video_vram_write((uint16_t)(a - 0xa000), v); return; }
    if (a >= 0x1000) return;
    if (a >= 0x80) { gc_ram[a] = v; return; }
    switch (a) {
    case 0x15: case 0x16: gc_ram[a] = v; scan_keys(); return;
    case 0x19: cpu.sys = v; gc_ram[a] = v; return;
    case 0x1c: cpu.sp = (uint16_t)((cpu.sp & 0xff) | v << 8); gc_ram[a] = v; return;
    case 0x1d: cpu.sp = (uint16_t)((cpu.sp & 0xff00) | v); gc_ram[a] = v; return;
    case 0x1e: cpu.ps0 = v; return;
    case 0x1f: cpu.ps1 = v; return;
    case 0x25: case 0x26: case 0x27: case 0x28: gc_ram[a] = v; map_window(a - 0x25); return;
    case 0x2b: gc_ram[a] = v; uart_left = 5000; gc_ram[0x2d] |= 2; return;
    case 0x34:
        gc_ram[a] = v;
        if (v & 0x80) { dma_armed = 1; dma_age = 0; }
        return;
    case 0x50: case 0x52: {
        int t = (a - 0x50) >> 1;
        gc_ram[a] = v; gc_ram[a + 1] = 0; timer_count[t] = 0;
        return;
    }
    case 0x51: case 0x53: reload[(a - 0x51) >> 1] = v; gc_ram[a] = 0; return;
    }
    gc_ram[a] = v;
}

static uint8_t timer_reload(int t) { return reload[t]; }

static void advance(int cycles) {
    if (gc_ram[0x1a] & 0x80) {
        uint32_t period = (gc_ram[0x1a] & 0x40) ? GC_CYCLES_PER_SECOND * 60u : GC_CYCLES_PER_SECOND;
        clock_accum += (uint32_t)cycles;
        if (clock_accum >= period) { clock_accum -= period; sm8500_raise(SM_IRQ_CK); }
    } else clock_accum = 0;
    if (cpu.stopped) return;
    if (dma_busy > 0 && (dma_busy -= cycles) <= 0) {
        dma_busy = 0;
        gc_ram[0x34] &= 0x7f;
        sm8500_raise(SM_IRQ_DMA);
    }
    if (dma_armed && (dma_age += cycles) > 2000) run_dma();
    if (uart_left > 0 && (uart_left -= cycles) <= 0) { uart_left = 0; sm8500_raise(SM_IRQ_UART); }
    for (int t = 0; t < 2; ++t) {
        uint8_t control = gc_ram[0x50 + 2 * t];
        if (!(control & 0x80)) continue;
        int divisor = timer_divisor[control & 7];
        timer_count[t] += cycles;
        if (timer_count[t] < divisor) continue;
        int ticks = divisor == 1 ? timer_count[t] : timer_count[t] / divisor;
        timer_count[t] -= ticks * divisor;
        uint16_t counter = (uint16_t)(0x51 + 2 * t);
        int value = gc_ram[counter] + ticks, limit = reload[t] ? reload[t] : 1;
        if (value < limit) { gc_ram[counter] = (uint8_t)value; continue; }
        /* Overflowed at least once; interrupts are edge events, a late slice raises one request. */
        value -= limit;
        gc_ram[counter] = (uint8_t)(value % limit);
        int enabled = t ? (gc_ram[0x11] & 0x40) : (gc_ram[0x10] & 0x40);
        int level = t ? ((cpu.ps0 & 7) < 4) : 1;
        if (enabled && (cpu.ps1 & SM_FI) && level) sm8500_raise(t ? SM_IRQ_TIM1 : SM_IRQ_TIM0);
    }
}

static int next_event(void) {
    int n = 512;
    for (int t = 0; t < 2; ++t) {
        uint8_t control = gc_ram[0x50 + 2 * t];
        if (!(control & 0x80)) continue;
        int divisor = timer_divisor[control & 7];
        int reload = timer_reload(t), counter = gc_ram[0x51 + 2 * t];
        int left = (reload > counter ? reload - counter : 1) * divisor - timer_count[t];
        if (left < n) n = left;
    }
    if (dma_busy > 0 && dma_busy < n) n = dma_busy;
    if (uart_left > 0 && uart_left < n) n = uart_left;
    return n < 1 ? 1 : n;
}

uint32_t gc_frames;

void gc_run_frame(void) {
    gc_frames++;
    int left = GC_CYCLES_PER_FRAME;
    gc_ram[0x32] &= 0x7f;
    while (left > 0 && !cpu.trapped) {
        int slice = next_event();
        if (slice > left) slice = left;
        if (cpu.halted && dma_armed) run_dma();
        if (cpu.halted || cpu.stopped) {
            if (cpu.pending) {
                cpu.cycles = 1;
                sm8500_run();
                if (!cpu.halted && !cpu.stopped) continue;
            }
            advance(slice);
            left -= slice;
            continue;
        }
        cpu.cycles = slice;
        sm8500_run();
        int used = slice - cpu.cycles;
        if (used < 1) used = 1;
        advance(used);
        left -= used;
    }
    gc_ram[0x32] |= 0x80;
    if ((gc_ram[0x10] & 1) && (cpu.ps1 & SM_FI) && (cpu.ps0 & 7) < 5) sm8500_raise(SM_IRQ_LCDC);
}

void gc_init(const gc_media_t *media) {
    gc_media = *media;
    for (int w = 0; w < 4; ++w) window[w] = unmapped;
    for (int w = 0; w < 4; ++w) map_window(w);
}
