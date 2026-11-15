#include <string.h>
extern int sendfail;
extern char aborted;
extern volatile int ticks;
extern void com_putc(char c);

void sendstr(char *s)
{
    unsigned i;

    sendfail = 0;
    for (i = 0; i < strlen(s); i++) {
        if (aborted == 1) {
            sendfail = 1;
            return;
        }
        com_putc(s[i]);
        ticks = 0;
        while (ticks < 10)
            ;
    }
    com_putc(13);
    ticks = 0;
    while (ticks < 10)
        ;
    com_putc(10);
    ticks = 0;
    while (ticks < 10)
        ;
}
