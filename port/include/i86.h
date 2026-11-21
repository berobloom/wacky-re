/* Watcom <i86.h> for the port: the 32-bit register unions in Watcom's own layout, so code that
 * fills them for int386() reads and writes the same fields. The services themselves are emulated by
 * the runtime (port/src/dos.c). */
#ifndef PORT_I86_H
#define PORT_I86_H

struct DWORDREGS { unsigned int eax, ebx, ecx, edx, esi, edi, cflag; };
struct WORDREGS {
    unsigned short ax, _1, bx, _2, cx, _3, dx, _4, si, _5, di, _6;
    unsigned int cflag;
};
struct BYTEREGS {
    unsigned char al, ah; unsigned short _1;
    unsigned char bl, bh; unsigned short _2;
    unsigned char cl, ch; unsigned short _3;
    unsigned char dl, dh; unsigned short _4;
};
union REGS { struct DWORDREGS x; struct WORDREGS w; struct BYTEREGS h; };
struct SREGS { unsigned short es, cs, ss, ds, fs, gs; };

int int386(int intno, union REGS *in, union REGS *out);
int int386x(int intno, union REGS *in, union REGS *out, struct SREGS *seg);
void sound(unsigned int hz);
void nosound(void);

/* flat model: a "far" pointer is a near one in the one 4 GB segment */
#define FP_OFF(p) ((unsigned int)(p))
#define FP_SEG(p) ((unsigned short)0)
#define MK_FP(s, o) ((void *)(o))
#endif
