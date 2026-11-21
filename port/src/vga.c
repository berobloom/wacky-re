/* vga.c: the VGA as the game programs it (findings/video.md): mode 13h from the BIOS, then
 * unchained to Mode X by modex_init (sequencer memory mode chain-4 off), 320x200 in four planes,
 * page flips through the CRTC start address, palette through the DAC.
 *
 * Documented hardware, not the game: the write path is the graphics controller's (write modes 0-3,
 * set/reset, rotate, logical function, bit mask, the four latches loaded by every read), gated per
 * plane by the sequencer map mask. Chain-4 (mode 13h) addresses plane a&3 at a&~3. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"

static uint8_t plane[4][0x10000];
static uint8_t latch[4];
static uint8_t seq[8], gc[9], crtc[0x19], attr[0x15];
static unsigned seq_i, gc_i, crtc_i, attr_i, attr_ff;
static uint8_t dac[256][3];
static unsigned dac_w, dac_r, dac_c, dac_rc;
static uint8_t misc = 0x63;
unsigned long vga_accesses;          /* reads + writes of the window (bus(), for measurements) */

/* The memory side of the bus cost: each access of the VGA window costs PORT_VGA_NS nanoseconds,
 * paid by the drawing thread in sleeps of at least 2 ms. Default 0, as in the harness's DOSBox-X
 * (vmemdelay defaults to off), whose screens the port is compared with; an ISA card of 1994 took
 * about 1000 ns a byte. The port I/O cost below is what paces the game like the original. */
static uint64_t bus_debt;
static long bus_ns = -1;

static void bus_pay(void)
{
    struct timespec a, b, d;
    uint64_t took;

    clock_gettime(CLOCK_MONOTONIC, &a);
    d.tv_sec = (time_t)(bus_debt / 1000000000u);
    d.tv_nsec = (long)(bus_debt % 1000000000u);
    nanosleep(&d, NULL);
    clock_gettime(CLOCK_MONOTONIC, &b);
    took = (uint64_t)(b.tv_sec - a.tv_sec) * 1000000000u + (uint64_t)(b.tv_nsec - a.tv_nsec);
    bus_debt = took >= bus_debt ? 0 : bus_debt - took;
}

/* A port access on the ISA bus takes about a microsecond, and the game's palette upload depends on
 * it: dac_load polls 0x3da for each entry, and with free reads it uploads several entries in one
 * horizontal blank and a fade takes 27 ms instead of the original's 0.3 s. The costs are DOSBox-X's
 * (src/hardware/iohandler.cpp, the harness's emulator): on the 8.33 MHz ISA clock an 8-bit
 * transfer is 8.5 clocks (1020 ns), a 16-bit one 5.5 (660 ns), a write 3/4 of a read. Too short to
 * sleep, so the caller spins. PORT_IO_NS scales them (the 8-bit read's ns; 0 turns them off). */
static long io_ns = -1;

static void io_cost(int write, int wide)
{
    struct timespec a, b;
    long want;

    if (io_ns < 0) {
        const char *e = getenv("PORT_IO_NS");
        io_ns = e ? atol(e) : 1020;
    }
    want = wide ? io_ns * 11 / 17 : io_ns;     /* 5.5 / 8.5 clocks */
    if (write)
        want = want * 3 / 4;
    if (want <= 0)
        return;
    clock_gettime(CLOCK_MONOTONIC, &a);
    do
        clock_gettime(CLOCK_MONOTONIC, &b);
    while ((b.tv_sec - a.tv_sec) * 1000000000L + (b.tv_nsec - a.tv_nsec) < want);
}

static inline void bus(void)
{
    if (bus_ns < 0) {
        const char *e = getenv("PORT_VGA_NS");
        bus_ns = e ? atol(e) : 0;
    }
    vga_accesses++;
    if (!bus_ns)
        return;
    bus_debt += (uint64_t)bus_ns;
    if (bus_debt >= 2000000u)
        bus_pay();
}

void vga_set_mode(int mode)
{
    memset(seq, 0, sizeof seq);
    memset(gc, 0, sizeof gc);
    memset(crtc, 0, sizeof crtc);
    seq[2] = 0x0f;
    gc[8] = 0xff;
    if (mode == 0x13) {
        seq[4] = 0x0e;                 /* chain-4, odd/even off, extended memory */
        gc[5] = 0x40;                  /* 256-colour shift */
        gc[6] = 0x05;                  /* graphics, A000 64 KB */
        crtc[0x09] = 0x41;             /* line doubling: 400 scan lines show 200 rows */
        crtc[0x12] = 0x8f;             /* vertical display end 399 (bit 8 in 0x07) */
        crtc[0x07] = 0x1f;
        crtc[0x13] = 0x28;             /* 40 words = 80 bytes per plane line */
        crtc[0x14] = 0x40;             /* doubleword mode */
        crtc[0x17] = 0xa3;
        memset(plane, 0, sizeof plane);
    } else {                           /* text mode 3: the game only returns to it on exit */
        seq[4] = 0x02;
        crtc[0x13] = 0x28;
    }
}

static int chain4(void) { return seq[4] & 8; }

uint8_t vga_read(uint32_t off)
{
    int p;

    bus();
    off &= 0xffff;
    if (chain4())
        return plane[off & 3][off & ~3u];
    for (p = 0; p < 4; p++)
        latch[p] = plane[p][off];
    if (gc[5] & 8) {                   /* read mode 1: colour compare */
        uint8_t r = 0xff;
        for (p = 0; p < 4; p++)
            if (gc[7] >> p & 1)
                r &= ~(latch[p] ^ ((gc[2] >> p & 1) ? 0xff : 0));
        return r;
    }
    return latch[gc[4] & 3];
}

static uint8_t alu(uint8_t v, uint8_t l)
{
    switch (gc[3] >> 3 & 3) {
    case 1: return v & l;
    case 2: return v | l;
    case 3: return v ^ l;
    }
    return v;
}

void vga_write(uint32_t off, uint8_t v)
{
    unsigned rot = gc[3] & 7, p;
    uint8_t mask = seq[2], bm = gc[8], r, raw = v;

    bus();
    off &= 0xffff;
    if (chain4()) {
        if (mask >> (off & 3) & 1)
            plane[off & 3][off & ~3u] = v;
        return;
    }
    v = (uint8_t)(v >> rot | v << ((8 - rot) & 7));
    for (p = 0; p < 4; p++) {
        if (!(mask >> p & 1))
            continue;
        switch (gc[5] & 3) {
        case 0:
            r = (gc[1] >> p & 1) ? ((gc[0] >> p & 1) ? 0xff : 0) : v;
            r = alu(r, latch[p]);
            r = (r & bm) | (latch[p] & ~bm);
            break;
        case 1:
            r = latch[p];
            break;
        case 2:
            r = alu((raw >> p & 1) ? 0xff : 0, latch[p]);     /* the unrotated colour's bit p */
            r = (r & bm) | (latch[p] & ~bm);
            break;
        default: {
            uint8_t m = v & bm, c = (gc[0] >> p & 1) ? 0xff : 0;
            r = (c & m) | (latch[p] & ~m);
        }
        }
        plane[p][off] = r;
    }
}

/* Input status 1 (0x3da) from a real-time clock, with mode 13h's / Mode X's timing (the CRTC values
 * the game sets: 100 character clocks a line of which 80 are shown, 449 lines a frame of which 400
 * are shown, vertical retrace on lines 412-413): a 31.47 kHz line, 70.1 Hz frame. Bit 0 (display
 * disabled) is set in each line's horizontal blank and in the vertical blank, bit 3 in the retrace.
 * The game paces itself on both: page_show waits for bit 3, and dac_load waits for a horizontal
 * blank before each palette entry, so a fade step (256 entries) takes 8 ms and a whole fade half a
 * second, as on the PC. */
#define LINE_NS 31778u                  /* 800 dots at 25.175 MHz */
#define FRAME_LINES 449u

static unsigned status1(void)
{
    struct timespec ts;
    uint64_t ns, line;
    unsigned fl, dot;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    line = ns / LINE_NS;
    fl = (unsigned)(line % FRAME_LINES);
    dot = (unsigned)(ns % LINE_NS);
    return (fl >= 400 || dot >= LINE_NS * 4 / 5 ? 1 : 0) | (fl >= 412 && fl < 414 ? 8 : 0);
}

static unsigned in8(unsigned port);
static void out8(unsigned port, unsigned v);

unsigned vga_in8(unsigned port) { io_cost(0, 0); return in8(port); }
void vga_out8(unsigned port, unsigned v) { io_cost(1, 0); out8(port, v); }
unsigned vga_in16(unsigned port) { io_cost(0, 1); return in8(port) | in8(port + 1) << 8; }
void vga_out16(unsigned port, unsigned v) { io_cost(1, 1); out8(port, v & 0xff); out8(port + 1, v >> 8 & 0xff); }

static unsigned in8(unsigned port)
{
    switch (port) {
    case 0x3c5: return seq[seq_i & 7];
    case 0x3cf: return gc_i < 9 ? gc[gc_i] : 0;
    case 0x3d5: return crtc_i < 0x19 ? crtc[crtc_i] : 0;
    case 0x3c4: return seq_i;
    case 0x3ce: return gc_i;
    case 0x3d4: return crtc_i;
    case 0x3cc: return misc;
    case 0x3c7: return 3;              /* DAC state: read mode */
    case 0x3c8: return dac_w;
    case 0x3c9: {
        uint8_t v = dac[dac_r & 255][dac_rc];
        if (++dac_rc == 3) {
            dac_rc = 0;
            dac_r++;
        }
        return v;
    }
    case 0x3da:
    case 0x3ba:
        attr_ff = 0;
        return status1();
    case 0x3c1: return attr_i < 0x15 ? attr[attr_i] : 0;
    }
    return 0xff;
}

static void out8(unsigned port, unsigned v)
{
    v &= 0xff;
    switch (port) {
    case 0x3c4: seq_i = v; break;
    case 0x3c5: seq[seq_i & 7] = v; break;
    case 0x3ce: gc_i = v; break;
    case 0x3cf: if (gc_i < 9) gc[gc_i] = v; break;
    case 0x3d4: crtc_i = v; break;
    case 0x3d5: if (crtc_i < 0x19) crtc[crtc_i] = v; break;
    case 0x3c2: misc = v; break;
    case 0x3c7: dac_r = v; dac_rc = 0; break;
    case 0x3c8: dac_w = v; dac_c = 0; break;
    case 0x3c9:
        dac[dac_w & 255][dac_c] = v & 0x3f;
        if (++dac_c == 3) {
            dac_c = 0;
            dac_w++;
        }
        break;
    case 0x3c0:
        if (!attr_ff)
            attr_i = v & 0x1f;
        else if (attr_i < 0x15)
            attr[attr_i] = v;
        attr_ff ^= 1;
        break;
    }
}

int vga_width(void) { return 320; }

int vga_height(void)
{
    unsigned vde = crtc[0x12] | (crtc[7] & 2) << 7 | (crtc[7] & 0x40) << 3;
    unsigned rows = (vde + 1) / ((crtc[9] & 0x1f) + 1) / ((crtc[9] & 0x80) ? 2 : 1);

    return rows >= 150 && rows <= 240 ? (int)rows : 200;
}

void vga_render(uint32_t *rgb)
{
    uint32_t pal[256];
    unsigned start = crtc[0x0c] << 8 | crtc[0x0d], stride = crtc[0x13] * 2u, x, y, i;
    int w = vga_width(), h = vga_height();

    for (i = 0; i < 256; i++) {
        unsigned r = dac[i][0], g = dac[i][1], b = dac[i][2];
        pal[i] = (r << 2 | r >> 4) << 16 | (g << 2 | g >> 4) << 8 | (b << 2 | b >> 4);
    }
    for (y = 0; y < (unsigned)h; y++)
        for (x = 0; x < (unsigned)w; x++) {
            uint8_t c;
            if (chain4()) {
                unsigned a = (start * 4 + y * 320 + x) & 0xffff;
                c = plane[a & 3][a & ~3u];
            } else
                c = plane[x & 3][(start + y * stride + (x >> 2)) & 0xffff];
            rgb[y * w + x] = pal[c];
        }
}
