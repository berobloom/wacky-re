/* Watcom <conio.h> for the port: port I/O goes to the emulated machine (port/src/machine.c), the
 * console calls to the keyboard layer. */
#ifndef PORT_CONIO_H
#define PORT_CONIO_H

unsigned int inp(unsigned int port);
unsigned int outp(unsigned int port, unsigned int value);
unsigned int inpw(unsigned int port);
unsigned int outpw(unsigned int port, unsigned int value);
int getch(void);
int kbhit(void);
#endif
