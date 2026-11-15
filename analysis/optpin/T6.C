#include <string.h>
extern int sendfail;
extern char aborted;
extern volatile int ticks;
extern void sendbyte(char c);

void sendstr(char *s)
{
    unsigned i;

    sendfail = 0;
    for (i = 0; i < strlen(s); i++) {
        if (aborted == 1) {
            sendfail = 1;
            return;
        }
        sendbyte(s[i]);
        ticks = 0;
        while (ticks < 10)
            ;
    }
    sendbyte(13);
    ticks = 0;
    while (ticks < 10)
        ;
    sendbyte(10);
    ticks = 0;
    while (ticks < 10)
        ;
}
