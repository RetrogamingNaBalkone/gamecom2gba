/* SM8500 / SM8521 instruction semantics for the game.com runtime.
 * Encodings follow the published SM8500 instruction set as modelled by MAME and
 * the Tigerbyte reference emulator used for this project's original captures. */
#include "sm8500.h"

#define IWRAM_CODE __attribute__((section(".iwram.code"), target("arm"), noinline))

__attribute__((section(".iwram.data"))) sm8500_t cpu;

static const uint8_t pair_reg[8] = { 0, 8, 2, 10, 4, 12, 6, 14 };

static inline uint8_t rb(uint16_t a) {
    if (a < 0x10) return cpu.reg[(cpu.ps0 & 0xf8) + a];
    return gc_read(a);
}
static inline void wb(uint16_t a, uint8_t v) {
    if (a < 0x10) { cpu.reg[(cpu.ps0 & 0xf8) + a] = v; return; }
    gc_write(a, v);
}
static inline uint16_t rw(uint16_t a) { return (uint16_t)(rb(a) << 8 | rb((uint16_t)(a + 1))); }
static inline void ww(uint16_t a, uint16_t v) { wb(a, (uint8_t)(v >> 8)); wb((uint16_t)(a + 1), (uint8_t)v); }
static inline uint8_t fetch(void) { return rb(cpu.pc++); }
static inline uint16_t fetchw(void) { uint16_t h = fetch(); return (uint16_t)(h << 8 | fetch()); }

static inline void push8(uint8_t v) { cpu.sp--; if (!(cpu.sys & 0x40)) cpu.sp &= 0xff; wb(cpu.sp, v); }
static inline uint8_t pop8(void) { uint8_t v = rb(cpu.sp); cpu.sp++; if (!(cpu.sys & 0x40)) cpu.sp &= 0xff; return v; }
static inline void push16(uint16_t v) { push8((uint8_t)v); push8((uint8_t)(v >> 8)); }
static inline uint16_t pop16(void) { uint16_t h = pop8(); return (uint16_t)(h << 8 | pop8()); }

static uint8_t add8(uint8_t a, uint8_t b, int carry) {
    unsigned c = carry && (cpu.ps1 & SM_FC), r = (unsigned)a + b + c;
    uint8_t f = cpu.ps1 & (SM_FB | SM_FI);
    if (r > 0xff) f |= SM_FC;
    if (!(r & 0xff)) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (!carry) { if (((b ^ a ^ 0x80) & (r ^ a)) & 0x80) f |= SM_FV; }
    else if (((b ^ a) & (r ^ a)) & 0x80) f |= SM_FV;
    if ((a ^ b ^ r) & 0x10) f |= SM_FH;
    cpu.ps1 = f;
    return (uint8_t)r;
}
static uint8_t sub8(uint8_t a, uint8_t b, int borrow, int store) {
    unsigned c = borrow && (cpu.ps1 & SM_FC), r = (unsigned)a - b - c;
    uint8_t f = cpu.ps1 & (store ? (SM_FB | SM_FI) : (SM_FB | SM_FI | SM_FH | SM_FD));
    if (r > 0xff) f |= SM_FC;
    if (!(r & 0xff)) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (((b ^ a) & (r ^ a)) & 0x80) f |= SM_FV;
    if (store) { f |= SM_FD; if ((a ^ b ^ r) & 0x10) f |= SM_FH; }
    cpu.ps1 = f;
    return (uint8_t)r;
}
static uint8_t logic8(uint8_t r) {
    uint8_t f = cpu.ps1 & (SM_FC | SM_FD | SM_FH | SM_FB | SM_FI);
    if (!r) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    cpu.ps1 = f;
    return r;
}
static void alu8(int op, uint16_t dst, uint8_t src) {
    uint8_t a = rb(dst), r;
    switch (op & 7) {
    case 0: sub8(a, src, 0, 0); return;
    case 1: r = add8(a, src, 0); break;
    case 2: r = sub8(a, src, 0, 1); break;
    case 3: r = add8(a, src, 1); break;
    case 4: r = sub8(a, src, 1, 1); break;
    case 5: r = logic8(a & src); break;
    case 6: r = logic8(a | src); break;
    default: r = logic8(a ^ src); break;
    }
    wb(dst, r);
}
static uint16_t add16(uint16_t a, uint16_t b, int carry) {
    unsigned c = carry && (cpu.ps1 & SM_FC), r = (unsigned)a + b + c;
    uint8_t f = cpu.ps1 & (SM_FB | SM_FI);
    if (r > 0xffff) f |= SM_FC;
    if (!(r & 0xffff)) f |= SM_FZ;
    if (r & 0x8000) f |= SM_FS;
    if (((b ^ a) & (r ^ a)) & 0x8000) f |= SM_FV;
    if ((a ^ b ^ r) & 0x10) f |= SM_FH;
    cpu.ps1 = f;
    return (uint16_t)r;
}
static uint16_t sub16(uint16_t a, uint16_t b, int borrow, int store) {
    unsigned c = borrow && (cpu.ps1 & SM_FC), r = (unsigned)a - b - c;
    uint8_t f = cpu.ps1 & (store ? (SM_FB | SM_FI) : (SM_FB | SM_FI | SM_FH | SM_FD));
    if (r > 0xffff) f |= SM_FC;
    if (!(r & 0xffff)) f |= SM_FZ;
    if (r & 0x8000) f |= SM_FS;
    if (((b ^ a) & (r ^ a)) & 0x8000) f |= SM_FV;
    if (store) { f |= SM_FD; if ((a ^ b ^ r) & 0x10) f |= SM_FH; }
    cpu.ps1 = f;
    return (uint16_t)r;
}
static uint16_t logic16(uint16_t r) {
    uint8_t f = cpu.ps1 & (SM_FC | SM_FD | SM_FH | SM_FB | SM_FI);
    if (!r) f |= SM_FZ;
    if (r & 0x8000) f |= SM_FS;
    cpu.ps1 = f;
    return r;
}
static void alu16(int op, uint16_t dst, uint16_t src) {
    uint16_t a = rw(dst), r;
    switch (op & 7) {
    case 0: sub16(a, src, 0, 0); return;
    case 1: r = add16(a, src, 0); break;
    case 2: r = sub16(a, src, 0, 1); break;
    case 3: r = add16(a, src, 1); break;
    case 4: r = sub16(a, src, 1, 1); break;
    case 5: r = logic16(a & src); break;
    case 6: r = logic16(a | src); break;
    default: r = logic16(a ^ src); break;
    }
    ww(dst, r);
}
static uint8_t inc8(uint8_t v) {
    uint8_t r = (uint8_t)(v + 1), f = cpu.ps1 & (SM_FC | SM_FD | SM_FH | SM_FB | SM_FI);
    if (!r) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (r == 0x00) f |= SM_FV;   /* reference behaviour: FF -> 00 */
    cpu.ps1 = f; return r;
}
static uint8_t dec8(uint8_t v) {
    uint8_t r = (uint8_t)(v - 1), f = cpu.ps1 & (SM_FC | SM_FD | SM_FH | SM_FB | SM_FI);
    if (!r) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (r == 0xff) f |= SM_FV;   /* reference behaviour: 00 -> FF */
    cpu.ps1 = f; return r;
}
static uint8_t neg8(uint8_t v) {
    uint8_t r = (uint8_t)-v, f = cpu.ps1 & (SM_FD | SM_FH | SM_FB | SM_FI);
    if (!r) f |= SM_FC | SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (r == 0x80) f |= SM_FV;
    cpu.ps1 = f; return r;
}
static uint8_t com8(uint8_t v) { return logic8((uint8_t)~v); }
/* kind: 0 rr, 1 rl, 2 rrc, 3 rlc, 4 srl, 5 sra, 6 sll */
static uint8_t shift8(int kind, uint8_t v) {
    uint8_t f = cpu.ps1 & (SM_FD | SM_FH | SM_FB | SM_FI), r = 0, c = (cpu.ps1 & SM_FC) != 0;
    switch (kind) {
    case 0: r = (uint8_t)(v >> 1 | v << 7); if (v & 1) f |= SM_FC; break;
    case 1: r = (uint8_t)(v << 1 | v >> 7); if (v & 0x80) f |= SM_FC; break;
    case 2: r = (uint8_t)(v >> 1 | c << 7); if (v & 1) f |= SM_FC; break;
    case 3: r = (uint8_t)(v << 1 | c); if (v & 0x80) f |= SM_FC; break;
    case 4: r = (uint8_t)(v >> 1); if (v & 1) f |= SM_FC; break;
    case 5: r = (uint8_t)(v >> 1 | (v & 0x80)); if (v & 1) f |= SM_FC; break;
    default: r = (uint8_t)(v << 1); if (v & 0x80) f |= SM_FC; break;
    }
    if (!r) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    if (kind == 1 || kind == 3) { if ((v ^ r) & 0x80) f |= SM_FV; }
    else if (kind == 0 || kind == 2) { if (((v ^ r) & 0x80) && !(r & 0x80)) f |= SM_FV; }
    cpu.ps1 = f;
    return r;
}
static uint8_t da8(uint8_t v) {
    unsigned r = v; uint8_t f = cpu.ps1;
    if (f & SM_FD) {
        if (f & SM_FC) r += (f & SM_FH) ? 0x9a : 0xa0;
        else if (f & SM_FH) r += 0xfa;
    } else if (f & SM_FC) {
        r += (f & SM_FH) ? 0x66 : ((r & 0x0f) < 10 ? 0x60 : 0x66);
    } else if (f & SM_FH) {
        if ((r & 0xf0) < 0xa0) r += 0x06; else { r += 0x66; f |= SM_FC; }
    } else if ((r & 0x0f) < 10) {
        if ((r & 0xf0) >= 0xa0) { r += 0x60; f |= SM_FC; }
    } else {
        if ((r & 0xf0) < 0x90) r += 0x06; else { r += 0x66; f |= SM_FC; }
    }
    f &= (uint8_t)~(SM_FZ | SM_FS);
    if (!(r & 0xff)) f |= SM_FZ;
    if (r & 0x80) f |= SM_FS;
    cpu.ps1 = f;
    return (uint8_t)r;
}
static uint8_t unary(int sub, uint8_t v) {   /* 0-15: clr neg com rr rl rrc rlc srl inc dec sra sll da swap */
    switch (sub) {
    case 0: return 0;
    case 1: return neg8(v);
    case 2: return com8(v);
    case 3: return shift8(0, v);
    case 4: return shift8(1, v);
    case 5: return shift8(2, v);
    case 6: return shift8(3, v);
    case 7: return shift8(4, v);
    case 8: return inc8(v);
    case 9: return dec8(v);
    case 10: return shift8(5, v);
    case 11: return shift8(6, v);
    case 12: return da8(v);
    default: return (uint8_t)(v >> 4 | v << 4);
    }
}
static int cond(int cc) {
    uint8_t f = cpu.ps1;
    int c = !!(f & SM_FC), z = !!(f & SM_FZ), s = !!(f & SM_FS), v = !!(f & SM_FV), r;
    switch (cc & 7) {
    case 0: r = 0; break;
    case 1: r = s ^ v; break;
    case 2: r = z | (s ^ v); break;
    case 3: r = c | z; break;
    case 4: r = v; break;
    case 5: r = s; break;
    case 6: r = z; break;
    default: r = c; break;
    }
    return (cc & 8) ? !r : r;
}

/* Register-indirect operand through an 8-bit pointer register (opcodes 20-29). */
static uint16_t ea_byte_pointer(uint8_t *dst) {
    uint8_t m = fetch(); uint16_t s;
    switch (m & 0xc0) {
    case 0x00: s = rb(m & 7); break;
    case 0x40: s = rb(m & 7); wb(m & 7, (uint8_t)(s + 1)); break;
    case 0x80: { uint8_t i = fetch(); s = (m & 7) ? (uint16_t)(i + rb(m & 7)) : i; } break;
    default: s = (uint8_t)(rb(m & 7) - 1); wb(m & 7, (uint8_t)s); break;
    }
    *dst = (m >> 3) & 7;
    return s;
}
/* Register-indirect operand through a 16-bit pointer pair (opcodes 30-3B). */
static uint16_t ea_word_pointer(uint8_t *dst, int step) {
    uint8_t m = fetch(); uint16_t s, p = pair_reg[m & 7];
    switch (m & 0xc0) {
    case 0x00: s = rw(p); break;
    case 0x40: s = rw(p); ww(p, (uint16_t)(s + step)); break;
    case 0x80: { uint16_t i = fetchw(); s = (m & 7) ? (uint16_t)(i + rw(p)) : i; } break;
    default: s = (uint16_t)(rw(p) - step); ww(p, s); break;
    }
    *dst = (m >> 3) & 7;
    return s;
}

static const uint8_t vector_of[SM_IRQ_LINES] = { 0x00, 0x02, 0x06, 0x08, 0x0e, 0x12, 0x16, 0x1a };
static const uint8_t enable_of[SM_IRQ_LINES][2] = {
    {0, 0x80}, {0, 0x40}, {0, 0x10}, {0, 0x08}, {0, 0x01}, {1, 0x40}, {1, 0x10}, {1, 0x04} };
static const uint8_t level_of[SM_IRQ_LINES] = { 8, 8, 7, 6, 5, 4, 3, 2 };
static const uint8_t request_of[SM_IRQ_LINES][2] = {
    {0, 0x80}, {0, 0x40}, {0, 0x10}, {0, 0x08}, {0, 0x01}, {1, 0x40}, {1, 0x10}, {1, 0x04} };

void sm8500_raise(int line) {
    cpu.pending |= (uint16_t)(1u << line);
    uint16_t ir = (uint16_t)(0x12 + request_of[line][0]);
    gc_write(ir, (uint8_t)(gc_read(ir) | request_of[line][1]));
}

extern uint16_t gc_vector(uint8_t index);   /* internal ROM vector table */
extern int gc_pcm_interrupt(void);
extern int gc_idle_loop(uint16_t branch, uint16_t target);

void sm8500_push_interrupt(void) {
    push8((uint8_t)cpu.pc);
    push8((uint8_t)(cpu.pc >> 8));
    push8(cpu.ps1);
    cpu.ps1 &= (uint8_t)~SM_FI;
}

static void take_interrupts(void) {
    uint8_t ir0 = gc_read(0x12), ir1 = gc_read(0x13);
    for (int line = 0; line < SM_IRQ_LINES; ++line) {
        uint16_t bit = (uint16_t)(1u << line);
        if (!(cpu.pending & bit)) continue;
        if (!((request_of[line][0] ? ir1 : ir0) & request_of[line][1])) { cpu.pending &= ~bit; continue; }
        cpu.halted = cpu.stopped = 0;
        uint8_t ie = gc_read((uint16_t)(0x10 + enable_of[line][0]));
        if (!(ie & enable_of[line][1]) || !(cpu.ps1 & SM_FI) || (cpu.ps0 & 7) >= level_of[line]) continue;
        cpu.pending &= ~bit;
        if (line == SM_IRQ_TIM1 && gc_pcm_interrupt()) {
            uint16_t ir = (uint16_t)(0x12 + request_of[line][0]);
            gc_write(ir, (uint8_t)(gc_read(ir) & ~request_of[line][1]));
            return;
        }
        sm8500_push_interrupt();
        cpu.pc = gc_vector(vector_of[line]);
        uint16_t ir = (uint16_t)(0x12 + request_of[line][0]);
        gc_write(ir, (uint8_t)(gc_read(ir) & ~request_of[line][1]));
        return;
    }
}

void sm8500_reset_state(void) {
    for (unsigned i = 0; i < sizeof cpu; ++i) ((uint8_t *)&cpu)[i] = 0;
}

/* Base cycle counts per opcode (MAME sm85ops.h annotations). */
static const uint8_t cycles_of[256] = {
    4,5,4,4,4,4,4,4,4,4,4,4,4,7,6,9, 5,5,5,5,5,5,5,5,8,8,6,7,12,12,12,12,
    7,7,7,7,7,7,7,7,6,8,6,6,6,4,4,6, 8,8,8,8,8,8,8,8,8,8,11,11,7,4,6,6,
    6,6,6,6,6,6,6,6,6,6,8,9,24,24,6,6, 6,6,6,6,6,6,6,6,6,6,7,6,47,44,9,8,
    9,10,10,10,10,14,14,14,9,10,10,10,10,13,13,13, 10,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    10,6,6,6,6,6,6,6,10,6,6,6,6,6,6,6, 6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    4,6,6,6,6,6,6,6,4,6,6,6,6,6,6,6, 4,6,6,6,6,6,6,6,4,6,6,6,6,6,6,6,
    4,6,6,6,6,6,6,6,4,6,6,6,6,6,6,6, 8,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    12,6,6,6,6,6,6,6,6,6,6,6,6,6,6,12, 2,2,6,6,6,6,6,6,10,12,2,2,2,2,2,2,
};

#ifdef SM8500_PROFILE
void sm8500_profile(uint16_t pc);
#endif

IWRAM_CODE void sm8500_run(void) {
    while (cpu.cycles > 0 && !cpu.trapped) {
        if (cpu.pending) take_interrupts();
        if (cpu.halted || cpu.stopped) return;
        uint16_t at = cpu.pc;
#ifdef SM8500_PROFILE
        sm8500_profile(at);
#endif
        uint8_t op = fetch(), m, d, a1, a2;
        uint16_t p, t;
        cpu.cycles -= cycles_of[op];
        if (op <= 0x0f) {
            uint8_t r = fetch(), v = rb(r);
            if (op == 0x0e) push8(v);
            else if (op == 0x0f) wb(r, pop8());
            else wb(r, unary(op, v));
        } else if (op <= 0x17) {
            m = fetch();
            if (!(m & 0xc0)) alu8(op, (m >> 3) & 7, rb(m & 7));
        } else switch (op) {
        case 0x18: a1 = fetch(); ww(a1, add16(rw(a1), 1, 0)); break;
        case 0x19: a1 = fetch(); ww(a1, sub16(rw(a1), 1, 0, 1)); break;
        case 0x1a: case 0x1b: {
            m = fetch();
            uint8_t pr = (m & 0xc0) ? m : (m >> 3) & 7;
            int sub = (m & 0xc0) ? 0 : (m & 7);
            uint16_t target = rb(pr);
            uint8_t v = rb(target);
            if (op == 0x1b && sub == 6) push8(v);
            else if (op == 0x1b && sub == 7) wb(target, pop8());
            else wb(target, unary(sub + (op == 0x1b ? 8 : 0), v));
            break;
        }
        case 0x1c: case 0x1d: {
            m = fetch(); uint8_t disp = fetch();
            p = (m & 0x38) ? (uint16_t)(disp + rb((m >> 3) & 7)) : (uint16_t)(0xff00 + disp);
            uint8_t v = rb(p), bit = (uint8_t)(1 << (m & 7));
            wb(p, op == 0x1c ? (uint8_t)(v & ~bit) : (uint8_t)(v | bit));
            break;
        }
        case 0x1e: a1 = fetch(); push16(rw(a1)); break;
        case 0x1f: a1 = fetch(); ww(a1, pop16()); break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
            p = ea_byte_pointer(&d); alu8(op, d, rb(p)); break;
        case 0x28: p = ea_byte_pointer(&d); wb(d, rb(p)); break;
        case 0x29: p = ea_byte_pointer(&d); wb(p, rb(d)); break;
        case 0x2a: case 0x2b: {
            m = fetch(); uint8_t disp = fetch(); int8_t off = (int8_t)fetch();
            p = (m & 0x38) ? (uint16_t)(disp + rb((m >> 3) & 7)) : (uint16_t)(0xff00 + disp);
            int set = (rb(p) >> (m & 7)) & 1;
            if (op == 0x2a ? !set : set) { cpu.pc = (uint16_t)(cpu.pc + off); cpu.cycles -= 4; }
            break;
        }
        case 0x2c: a1 = fetch(); t = rw(a1); ww(a1, (t & 0x80) ? (uint16_t)(t | 0xff00) : (uint16_t)(t & 0xff)); break;
        case 0x2e: cpu.ps0 = fetch(); break;
        case 0x2f: a1 = fetch(); a2 = fetch(); cpu.ps1 &= (uint8_t)~(SM_FV | SM_FZ);
            if (!(rb(a1) & a2)) cpu.ps1 |= SM_FZ; break;
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
            p = ea_word_pointer(&d, 1); alu8(op, d, rb(p)); break;
        case 0x38: p = ea_word_pointer(&d, 1); wb(d, rb(p)); break;
        case 0x39: p = ea_word_pointer(&d, 1); wb(p, rb(d)); break;
        case 0x3a: p = ea_word_pointer(&d, 2); ww(pair_reg[d], rw(p)); break;
        case 0x3b: p = ea_word_pointer(&d, 2); ww(p, rw(pair_reg[d])); break;
        case 0x3c: m = fetch(); if (!(m & 0xc0)) ww(pair_reg[(m >> 3) & 7], rw(pair_reg[m & 7])); break;
        case 0x3e: case 0x3f:
            m = fetch();
            if (!(m & 0xc0)) t = rw(pair_reg[m & 7]);
            else if ((m & 0xc0) == 0x40) {
                uint16_t e = fetchw();
                if (m & 0x38) e = (uint16_t)(e + rb((m >> 3) & 7));
                t = rw(e);
            } else { cpu.trapped = 1; cpu.trap_pc = at; return; }
            if (op == 0x3f) push16(cpu.pc);
            cpu.pc = t;
            break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            a1 = fetch(); a2 = fetch(); alu8(op, a2, rb(a1)); break;
        case 0x48: a1 = fetch(); a2 = fetch(); wb(a2, rb(a1)); break;
        case 0x49: t = fetchw(); push16(cpu.pc); cpu.pc = t; break;
        case 0x4a: a1 = fetch(); a2 = fetch(); ww(a2, rw(a1)); break;
        case 0x4b: a1 = fetch(); t = fetchw(); ww(a1, t); break;
        case 0x4c: case 0x4d: {
            a1 = fetch(); a2 = fetch();
            uint16_t r = (uint16_t)(rb(a2 | 1) * (op == 0x4c ? rb(a1) : a1));
            ww(a2, r);
            cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV);
            if (!r) cpu.ps1 |= SM_FZ;
            break;
        }
        case 0x4e: {
            a1 = fetch(); a2 = fetch(); uint8_t bit = (uint8_t)(1 << (a1 & 7));
            if ((a1 & 0xc0) == 0x40) { uint8_t v = rb(a2); wb(a2, (cpu.ps1 & SM_FB) ? (uint8_t)(v | bit) : (uint8_t)(v & ~bit)); }
            else if (!(a1 & 0xc0)) { cpu.ps1 &= (uint8_t)(SM_FC | SM_FS | SM_FD | SM_FH | SM_FI); cpu.ps1 |= (rb(a2) & bit) ? SM_FB : SM_FZ; }
            break;
        }
        case 0x4f: {
            a1 = fetch(); a2 = fetch(); uint8_t bit = a1 & 7;
            uint8_t s1 = (uint8_t)(rb(a2) & (1 << bit)), s2 = (uint8_t)(((cpu.ps1 & SM_FB) >> 1) << bit);
            switch (a1 & 0xc0) {
            case 0x00: cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV); if (s1 == s2) cpu.ps1 |= SM_FZ; break;
            case 0x40: cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV | SM_FB); cpu.ps1 |= (s1 & s2) ? SM_FB : SM_FZ; break;
            case 0x80: cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV | SM_FB); cpu.ps1 |= (s1 | s2) ? SM_FB : SM_FZ; break;
            default: cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV | SM_FB); cpu.ps1 |= (s1 ^ s2) ? SM_FB : SM_FZ; break;
            }
            break;
        }
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            a1 = fetch(); a2 = fetch(); alu8(op, a2, a1); break;
        case 0x58: a1 = fetch(); a2 = fetch(); wb(a2, a1); break;
        case 0x5a: case 0x5b: {
            m = fetch(); uint16_t pair = pair_reg[m & 7]; uint8_t imm;
            switch (m & 0xc0) {
            case 0x00: p = rw(pair); imm = fetch(); break;
            case 0x40: p = rw(pair); ww(pair, (uint16_t)(p + 1)); imm = fetch(); break;
            case 0x80: { uint8_t disp = fetch(); p = (uint16_t)(rw(pair) + disp); imm = fetch(); } break;
            default: p = (uint16_t)(rw(pair) - 1); ww(pair, p); imm = fetch(); break;
            }
            if (op == 0x5a) sub8(rb(p), imm, 0, 0); else wb(p, imm);
            break;
        }
        case 0x5c: {
            a1 = fetch(); a2 = fetch(); cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV);
            uint8_t dv = rb((uint16_t)(a1 + 1));
            if (dv) { uint16_t n = rw(a2), q = (uint16_t)(n / dv); wb(a1, (uint8_t)(n % dv)); ww(a2, q); if (!q) cpu.ps1 |= SM_FZ; }
            else cpu.ps1 |= SM_FV;
            break;
        }
        case 0x5d:
            a1 = fetch(); a2 = fetch(); cpu.ps1 &= (uint8_t)~(SM_FZ | SM_FV);
            if (a1) { uint16_t q = (uint16_t)(rw(a2) / a1); ww(a2, q); if (!q) cpu.ps1 |= SM_FZ; }
            else cpu.ps1 |= SM_FV;
            break;
        case 0x5e: a1 = fetch(); m = fetch(); a2 = fetch(); wb(a1, (uint8_t)((rb(a1) & m) | rb(a2))); break;
        case 0x5f: a1 = fetch(); m = fetch(); a2 = fetch(); wb(a1, (uint8_t)((rb(a1) & m) | a2)); break;
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
            a1 = fetch(); a2 = fetch(); alu16(op, a2, rw(a1)); break;
        case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
            a1 = fetch(); t = fetchw(); alu16(op, a1, t); break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77: {
            int8_t off = (int8_t)fetch(); uint8_t v = (uint8_t)(rb(op & 7) - 1);
            wb(op & 7, v);
            if (v) { cpu.pc = (uint16_t)(cpu.pc + off); cpu.cycles -= 4; }
            break;
        }
        case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
            t = fetchw(); ww(pair_reg[op & 7], t); break;
        default:
            if (op >= 0x80 && op <= 0x8f) {
                uint8_t r = fetch(); int8_t off = (int8_t)fetch();
                int set = (rb(r) >> (op & 7)) & 1;
                if ((op & 8) ? set : !set) {
                    cpu.pc = (uint16_t)(cpu.pc + off); cpu.cycles -= 4;
                    if (off < 0 && gc_idle_loop(at, cpu.pc)) { cpu.halted = 1; return; }
                }
            } else if (op >= 0x90 && op <= 0x9f) {
                t = fetchw(); if (cond(op & 15)) cpu.pc = t;
            } else if (op >= 0xa0 && op <= 0xaf) {
                uint8_t r = fetch(), v = rb(r), bit = (uint8_t)(1 << (op & 7));
                wb(r, (op & 8) ? (uint8_t)(v | bit) : (uint8_t)(v & ~bit));
            } else if (op >= 0xb0 && op <= 0xb7) { uint8_t r = fetch(); wb(op & 7, rb(r)); }
            else if (op >= 0xb8 && op <= 0xbf) { uint8_t r = fetch(); wb(r, rb(op & 7)); }
            else if (op >= 0xc0 && op <= 0xc7) { uint8_t i = fetch(); wb(op & 7, i); }
            else if (op >= 0xc8 && op <= 0xcf) { uint8_t i = fetch(); wb((uint16_t)(0x10 + (op & 7)), i); }
            else if (op >= 0xd0 && op <= 0xdf) {
                int8_t off = (int8_t)fetch();
                if (cond(op & 15)) { cpu.pc = (uint16_t)(cpu.pc + off); cpu.cycles -= 4; }
            } else if (op >= 0xe0 && op <= 0xef) {
                uint8_t i = fetch(); push16(cpu.pc); cpu.pc = (uint16_t)(0x1000 | (op & 15) << 8 | i);
            } else switch (op) {
                case 0xf0: cpu.stopped = 1; break;
                case 0xf1: cpu.halted = 1; break;
                case 0xf8: cpu.pc = pop16(); break;
                case 0xf9: cpu.ps1 = pop8(); cpu.pc = pop16(); break;
                case 0xfa: cpu.ps1 &= (uint8_t)~SM_FC; break;
                case 0xfb: cpu.ps1 ^= SM_FC; break;
                case 0xfc: cpu.ps1 |= SM_FC; break;
                case 0xfd: cpu.ps1 |= SM_FI; break;
                case 0xfe: cpu.ps1 &= (uint8_t)~SM_FI; break;
                case 0xff: break;
                default: cpu.trapped = 1; cpu.trap_pc = at; return;
            }
        }
    }
}
