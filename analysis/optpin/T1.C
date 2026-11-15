#include <stdio.h>
#include <i86.h>

FILE *datfile;
int scrollpos;

void seekdat(long ofs)
{
    fseek(datfile, ofs, SEEK_SET);
}

void readdat(void *buf, int n)
{
    fread(buf, n, 1, datfile);
}

int fromscroll(int x)
{
    return scrollpos - x;
}

void setvmode(char m)
{
    union REGS r;
    r.h.al = m;
    r.h.ah = 0;
    int386(0x10, &r, &r);
}
