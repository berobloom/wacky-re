/* Watcom <dos.h> for the port: interrupt vectors are kept by the runtime (port/src/machine.c), which
 * calls an installed handler at its poll points instead of on a hardware interrupt. */
#ifndef PORT_DOS_H
#define PORT_DOS_H
#include <i86.h>

#define __interrupt
#define __far
#define __near
#define _interrupt
#define _far

typedef void (*port_isr)();
port_isr _dos_getvect(unsigned int vec);
void _dos_setvect(unsigned int vec, port_isr fn);
#endif
