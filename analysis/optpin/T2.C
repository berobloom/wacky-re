#include <stdio.h>
#include <conio.h>

extern FILE *datfile;
extern int ext41475(int);

unsigned short crcstep(unsigned short crc, unsigned short b);
short crcidx;
unsigned short crctab[256];

unsigned short crcstep(unsigned short crc, unsigned short b)
{
    crcidx = (crc >> 8) ^ b;
    crc = (crc << 8) ^ crctab[crcidx];
    return crc;
}

int xalloc(int *p, int x)
{
    int r;
    r = ext41475(x);
    if (r == 0) return -1;
    *p = r;
    return 0;
}

long telldat(void)
{
    return ftell(datfile);
}

struct E22 { char a[22 - 4 - 2]; short s; long v; };
struct E22 tab22[10];
short cur22;

long get22(void)
{
    return tab22[cur22].v;
}

struct E12 { short v; short pad; short x; short y; char r[4]; };
struct E12 tab12[8];

short find12(short x, short y)
{
    int i;
    for (i = 0; i <= 7; i++)
        if (tab12[i].x == x && tab12[i].y == y) return tab12[i].v;
    return -1;
}

void waitkey(void)
{
    while (kbhit()) getch();
    printf("    \n");
    printf("Press any key when ready..\n");
    getch();
}
