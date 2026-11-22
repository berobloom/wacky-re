/* Watcom clib functions that glibc lacks, force-included ahead of src/ww.c (port/src/clib.c). */
#ifndef PORT_CLIB_H
#define PORT_CLIB_H
char *itoa(int value, char *buf, int radix);
char *strupr(char *s);
/* a pointer the game reads through while it is NULL: under DOS/4GW that reads linear 0, the
 * interrupt table; here a copy of it (port/src/lowmem.c) */
extern const unsigned char port_lowmem[0x500];
#define LOWMEM(p) ((p) ? (p) : (void *)port_lowmem)
#endif
