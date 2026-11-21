/* Watcom clib functions that glibc lacks, force-included ahead of src/ww.c (port/src/clib.c). */
#ifndef PORT_CLIB_H
#define PORT_CLIB_H
char *itoa(int value, char *buf, int radix);
char *strupr(char *s);
#endif
