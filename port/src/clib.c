/* clib.c: what src/ww.c takes from Watcom's clib and DOS that glibc does not provide.
 *
 * rand/srand are Watcom's, not glibc's: the game's choices (AI, pickups, demo seeds) follow its
 * sequence, so the port must draw the same numbers. Watcom's documented LCG; NOT yet checked
 * against the game's own rand (to do: run it under Unicorn, tools/le/emu.py, and compare). */
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <conio.h>
#include <dos.h>
#include <i86.h>

#include "machine.h"

/* ---- Watcom clib */
static uint32_t rand_next = 1;

int rand(void)
{
    rand_next = rand_next * 1103515245u + 12345u;
    return (int)(rand_next >> 16 & 0x7fff);
}

void srand(unsigned seed) { rand_next = seed; }

char *itoa(int value, char *buf, int radix)
{
    char tmp[34], *p = tmp, *o = buf;
    unsigned u = (unsigned)value;

    if (radix == 10 && value < 0) {
        *o++ = '-';
        u = -(unsigned)value;
    }
    do {
        unsigned d = u % radix;
        *p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        u /= radix;
    } while (u);
    while (p > tmp)
        *o++ = *--p;
    *o = 0;
    return buf;
}

char *strupr(char *s)
{
    char *p;

    for (p = s; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p -= 32;
    return s;
}

/* 0x430f7: the dword path of clib's __stos (src/ww.c fn_1a2f8): ecx dwords of edx at eax */
void fn_430f7(void *dst, int value, int count)
{
    uint32_t *d = dst;

    while (count-- > 0)
        *d++ = (uint32_t)value;
}

struct dostime { unsigned char hund, sec; unsigned short minhrs; unsigned char dst; };

/* 0x43164: INT 21h time into the game's 5-byte record (src/ww.c dos_gettime) */
void dos_gettime(struct dostime *t)
{
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    t->hund = (unsigned char)(ts.tv_nsec / 10000000);
    t->sec = (unsigned char)tm.tm_sec;
    t->minhrs = (unsigned short)(tm.tm_min | tm.tm_hour << 8);
    t->dst = 0;
}

void sound(unsigned hz) { (void)hz; }      /* PC speaker: silent for now */
void nosound(void) {}

/* ---- ports */
unsigned int inp(unsigned int port) { return port_in8(port); }
unsigned int outp(unsigned int port, unsigned int value) { port_out8(port, value); return value; }
unsigned int inpw(unsigned int port) { return port_in16(port); }
unsigned int outpw(unsigned int port, unsigned int value) { port_out16(port, value & 0xffff); return value; }

/* ---- interrupts */
port_isr _dos_getvect(unsigned int v) { return isr_get(v); }
void _dos_setvect(unsigned int v, port_isr fn) { isr_set(v, fn); }

int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *seg)
{
    union REGS r = *in;

    (void)seg;
    r.x.cflag = 0;
    switch (intno) {
    case 0x10:
        switch (r.h.ah) {
        case 0x00:                       /* set mode */
            vga_set_mode(r.h.al & 0x7f);
            break;
        case 0x0f:                       /* get mode */
            r.h.al = 0x13;
            r.h.ah = 40;
            break;
        case 0x1a:                       /* display combination: VGA with colour display */
            r.h.al = 0x1a;
            r.h.bl = 0x08;
            r.h.bh = 0;
            break;
        case 0x12:                       /* alternate select (palette loading on/off): accepted */
            r.h.al = 0x12;
            break;
        default:
            break;
        }
        break;
    case 0x21:
        switch (r.h.ah) {
        case 0x25:                       /* set vector: EDX is the handler (flat model) */
            isr_set(r.h.al, (port_isr)(uintptr_t)r.x.edx);
            break;
        case 0x35:                       /* get vector: EBX */
            r.x.ebx = (unsigned)(uintptr_t)isr_get(r.h.al);
            if (seg)
                seg->es = 0;
            break;
        case 0x2c: {                     /* get time */
            struct dostime t;
            dos_gettime(&t);
            r.h.ch = t.minhrs >> 8;
            r.h.cl = t.minhrs & 0xff;
            r.h.dh = t.sec;
            r.h.dl = t.hund;
            break;
        }
        default:
            port_fatal("int 21h ah=%02xh not emulated", r.h.ah);
        }
        break;
    case 0x14:                           /* BIOS serial: no port there (timeout bit) */
        r.h.ah = 0x80;
        break;
    default:
        port_fatal("int %02xh not emulated", intno);
    }
    *out = r;
    return (int)r.x.eax;
}

int int386(int intno, union REGS *in, union REGS *out)
{
    return int386x(intno, in, out, NULL);
}
