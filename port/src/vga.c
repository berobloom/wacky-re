/* vga.c: the VGA as the game programs it (findings/video.md): mode 13h from the BIOS, then
 * unchained to Mode X by modex_init (sequencer memory mode chain-4 off), 320x200 in four planes,
 * page flips through the CRTC start address, palette through the DAC.
 *
 * Documented hardware, not the game: the write path is the graphics controller's (write modes 0-3,
 * set/reset, rotate, logical function, bit mask, the four latches loaded by every read), gated per
 * plane by the sequencer map mask. Chain-4 (mode 13h) addresses plane a&3 at a&~3. */
#include <stdint.h>
#include <string.h>

#include "machine.h"

static uint8_t plane[4][0x10000];
static uint8_t latch[4];
static uint8_t seq[8], gc[9], crtc[0x19], attr[0x15];
static unsigned seq_i, gc_i, crtc_i, attr_i, attr_ff;
static uint8_t dac[256][3];
static unsigned dac_w, dac_r, dac_c, dac_rc;
static uint8_t misc = 0x63;

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

/* the vertical retrace: 70 Hz, retrace the first 1.4 ms of each period (real time, so the game's
 * waits pace it as they paced the PC) */
static unsigned status1(void)
{
    uint32_t us = machine_ms() * 1000u % 14286u;
    static unsigned hblank;

    hblank ^= 1;                       /* display enable toggles per read: hblank waits end */
    return (us < 1400 ? 8 | 1 : hblank);
}

unsigned vga_in8(unsigned port)
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

void vga_out8(unsigned port, unsigned v)
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
