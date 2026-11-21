/* cpu.h: the register file and helpers the translated asm module runs on (port/tools/asm2c.py).
 *
 * One register file and stack per thread: the game thread runs the drawing routines, the hardware
 * thread runs kbd_isr (int 9), and an interrupt must not see the code it interrupted half-way.
 * Addresses are the process's own (32-bit build, data object at its game address); only the VGA
 * window 0xa0000-0xaffff is not memory and goes to vga.c. */
#ifndef PORT_CPU_H
#define PORT_CPU_H
#include <stdint.h>
#include <string.h>

#include "machine.h"

struct cpu {
    uint32_t r[8];                 /* eax ecx edx ebx esp ebp esi edi */
    uint8_t zf, sf, cf, of;
    int depth;
};

extern __thread struct cpu cpu_tls;
extern __thread uint8_t cpu_stack[0x10000];

static inline struct cpu *cpu_get(void) { return &cpu_tls; }

static inline struct cpu *cpu_enter(void)
{
    struct cpu *c = &cpu_tls;

    if (c->depth++ == 0)
        c->r[4] = (uint32_t)(uintptr_t)(cpu_stack + sizeof cpu_stack - 16);
    return c;
}

static inline void cpu_leave(void) { cpu_tls.depth--; }

/* ---- memory */
static inline uint8_t rd8(uint32_t a)
{
    return is_vga(a) ? vga_read(a - VGA_WIN) : *(uint8_t *)(uintptr_t)a;
}
static inline void wr8(uint32_t a, uint8_t v)
{
    if (is_vga(a))
        vga_write(a - VGA_WIN, v);
    else
        *(uint8_t *)(uintptr_t)a = v;
}
static inline uint16_t rd16(uint32_t a)
{
    uint16_t v;
    if (is_vga(a) || is_vga(a + 1))
        return rd8(a) | rd8(a + 1) << 8;
    memcpy(&v, (void *)(uintptr_t)a, 2);
    return v;
}
static inline void wr16(uint32_t a, uint16_t v)
{
    if (is_vga(a) || is_vga(a + 1)) {
        wr8(a, (uint8_t)v);
        wr8(a + 1, (uint8_t)(v >> 8));
    } else
        memcpy((void *)(uintptr_t)a, &v, 2);
}
static inline uint32_t rd32(uint32_t a)
{
    uint32_t v;
    if (is_vga(a) || is_vga(a + 3))
        return rd16(a) | (uint32_t)rd16(a + 2) << 16;
    memcpy(&v, (void *)(uintptr_t)a, 4);
    return v;
}
static inline void wr32(uint32_t a, uint32_t v)
{
    if (is_vga(a) || is_vga(a + 3)) {
        wr16(a, (uint16_t)v);
        wr16(a + 2, (uint16_t)(v >> 16));
    } else
        memcpy((void *)(uintptr_t)a, &v, 4);
}

/* rep movsb (DF = 0): a byte at a time, as the CPU does, so an overlapping copy and a VGA latch
 * copy (write mode 1) behave as on the PC; plain memory without overlap takes memcpy */
static inline void rep_movsb(struct cpu *c)
{
    uint32_t n = c->r[1], s = c->r[6], d = c->r[7];

    if (!n)
        return;
    if (!is_vga(s) && !is_vga(s + n - 1) && !is_vga(d) && !is_vga(d + n - 1) &&
        (d + n <= s || s + n <= d) && s + n > s && d + n > d)
        memcpy((void *)(uintptr_t)d, (void *)(uintptr_t)s, n);
    else
        for (uint32_t k = 0; k < n; k++)
            wr8(d + k, rd8(s + k));
    c->r[6] = s + n;
    c->r[7] = d + n;
    c->r[1] = 0;
}

/* ---- stack */
static inline void push32(struct cpu *c, uint32_t v) { c->r[4] -= 4; wr32(c->r[4], v); }
static inline uint32_t pop32(struct cpu *c) { uint32_t v = rd32(c->r[4]); c->r[4] += 4; return v; }
static inline void push16(struct cpu *c, uint16_t v) { c->r[4] -= 2; wr16(c->r[4], v); }
static inline uint16_t pop16(struct cpu *c) { uint16_t v = rd16(c->r[4]); c->r[4] += 2; return v; }

/* ---- flags, for the instructions whose flags something reads */
#define FLAGS(B, T, ST)                                                                              \
    static inline void flags_logic##B(struct cpu *c, T r)                                            \
    {                                                                                                \
        c->zf = r == 0; c->sf = (ST)r < 0; c->cf = 0; c->of = 0;                                     \
    }                                                                                                \
    static inline void flags_sub##B(struct cpu *c, T a, T b)                                         \
    {                                                                                                \
        T r = (T)(a - b);                                                                            \
        c->zf = r == 0; c->sf = (ST)r < 0; c->cf = a < b;                                            \
        c->of = (ST)((a ^ b) & (a ^ r)) < 0;                                                         \
    }                                                                                                \
    static inline T op_sub##B(struct cpu *c, T a, T b) { flags_sub##B(c, a, b); return (T)(a - b); } \
    static inline T op_add##B(struct cpu *c, T a, T b)                                               \
    {                                                                                                \
        T r = (T)(a + b);                                                                            \
        c->zf = r == 0; c->sf = (ST)r < 0; c->cf = r < a;                                            \
        c->of = (ST)((a ^ r) & (b ^ r)) < 0;                                                         \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_adc##B(struct cpu *c, T a, T b)                                               \
    {                                                                                                \
        T k = c->cf, r = (T)(a + b + k);                                                             \
        c->zf = r == 0; c->sf = (ST)r < 0; c->cf = k ? r <= a : r < a;                               \
        c->of = (ST)((a ^ r) & (b ^ r)) < 0;                                                         \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_and##B(struct cpu *c, T a, T b) { T r = a & b; flags_logic##B(c, r); return r; } \
    static inline T op_or##B(struct cpu *c, T a, T b) { T r = a | b; flags_logic##B(c, r); return r; }  \
    static inline T op_xor##B(struct cpu *c, T a, T b) { T r = a ^ b; flags_logic##B(c, r); return r; } \
    static inline T op_inc##B(struct cpu *c, T a)                                                    \
    {                                                                                                \
        T r = (T)(a + 1);                                                                            \
        c->zf = r == 0; c->sf = (ST)r < 0; c->of = (ST)(~a & r) < 0;                                 \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_dec##B(struct cpu *c, T a)                                                    \
    {                                                                                                \
        T r = (T)(a - 1);                                                                            \
        c->zf = r == 0; c->sf = (ST)r < 0; c->of = (ST)(a & ~r) < 0;                                 \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_shl##B(struct cpu *c, T a, unsigned n)                                        \
    {                                                                                                \
        T r;                                                                                         \
        if (!n) return a;                                                                            \
        r = n < B ? (T)(a << n) : 0;                                                                 \
        c->cf = n <= B ? (a >> (B - n)) & 1 : 0;                                                     \
        c->zf = r == 0; c->sf = (ST)r < 0; c->of = ((ST)r < 0) ^ c->cf;                              \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_shr##B(struct cpu *c, T a, unsigned n)                                        \
    {                                                                                                \
        T r;                                                                                         \
        if (!n) return a;                                                                            \
        r = n < B ? (T)(a >> n) : 0;                                                                 \
        c->cf = n <= B ? (a >> (n - 1)) & 1 : 0;                                                     \
        c->zf = r == 0; c->sf = (ST)r < 0; c->of = (ST)a < 0;                                        \
        return r;                                                                                    \
    }                                                                                                \
    static inline T op_sar##B(struct cpu *c, T a, unsigned n)                                        \
    {                                                                                                \
        T r;                                                                                         \
        if (!n) return a;                                                                            \
        r = (T)((ST)a >> (n < B ? n : B - 1));                                                       \
        c->cf = ((ST)a >> (n - 1 < B ? n - 1 : B - 1)) & 1;                                          \
        c->zf = r == 0; c->sf = (ST)r < 0; c->of = 0;                                                \
        return r;                                                                                    \
    }

FLAGS(8, uint8_t, int8_t)
FLAGS(16, uint16_t, int16_t)
FLAGS(32, uint32_t, int32_t)
#undef FLAGS
#endif
