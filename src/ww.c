/* Wacky Wheels 1.1 (WW.EXE): the game's C module, rebuilt to compile to the same bytes.
 *
 * One translation unit (findings/executables.md, Code layout), in address order from 0x10260.
 * Compile: tools/wc/wcc.sh .work/toolchains/inst/WC95B src WW.C -s -3r -oneatx ; check: src/check.sh
 * Globals the code does not define yet are extern; their addresses are in analysis/ww_names.txt.
 * Names are ours (levels in ww_names.txt).
 */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>
#include <stdlib.h>

/* ---- serial link: CRC (the public-domain table CRC: crchware / mk_crctbl / crcupdate) */

extern unsigned short crctbl[256];

unsigned short crchware(unsigned short data, unsigned short genpoly, unsigned short accum)
{
    static short i;

    data <<= 8;
    for (i = 8; i > 0; i--) {
        if ((data ^ accum) & 0x8000)
            accum = (accum << 1) ^ genpoly;
        else
            accum <<= 1;
        data <<= 1;
    }
    return accum;
}

void mk_crctbl(unsigned short poly)
{
    unsigned short i;

    for (i = 0; i < 256; i++)
        crctbl[i] = crchware(i, poly, 0);
}

unsigned short crcupdate(unsigned short accum, unsigned short data)
{
    static short comb_val;

    comb_val = (accum >> 8) ^ data;
    accum = (accum << 8) ^ crctbl[comb_val];
    return accum;
}

/* ---- serial link: the UART */

extern union REGS comregs;
extern short comport, comirq, uarttype, comvec;
extern char hasfifo;
extern void (__interrupt __far *oldcomvec)();
extern volatile short txcount, txtail, txhead;
extern volatile short rxhead, rxtail, rxcount;
extern unsigned char rxbuf[0x4000];
extern int comdelay;
extern int comtask;
extern volatile int comticks;
extern unsigned char txbuf[0x4000];
extern int rxints, txints, overruns, parityerrs, framingerrs, breaks;
extern short lsr;
extern int sendfail;
extern char aborted;
extern volatile int ticks;

void __interrupt __far com_isr(void);
void __interrupt __far com_isr_fifo(void);
void disable_ints(void);
void enable_ints(void);
void lib_40ff0(int, int);

/* differs: the stack slots of port, irq, comnum are permuted (QUESTIONS.md 2) */
void com_open(short port, short irq, short baud, short comnum)
{
    int rate;
    unsigned char divisor;
    unsigned char iir;

    mk_crctbl(0x1021);
    txcount = 0;
    txtail = 0;
    txhead = 0;
    rxcount = 0;
    rxtail = 0;
    rxhead = 0;
    comport = port;
    comirq = irq;
    comregs.w.ax = 0xf3;
    comregs.w.dx = comnum - 1;
    int386(0x14, &comregs, &comregs);
    if (baud == 14400)
        rate = 19200;
    else
        rate = baud;
    divisor = 1843200UL / (rate * 16);
    outp(comport + 3, 0x80);
    outp(comport + 1, 0);
    outp(comport, divisor);
    outp(comport + 3, 3);
    outp(comport + 2, 0x41);
    iir = inp(comport + 2) & 0xf8;
    if (iir == 0xc0) {
        uarttype = 1;
        hasfifo = 1;
    } else {
        uarttype = 2;
        hasfifo = 0;
        outp(comport + 2, 0);
    }
    outp(comport + 1, 0);
    outp(comport + 4, (inp(comport + 4) & 0xe4) | 0x0b);
    inp(comport);
    inp(comport + 2);
    comvec = comirq + 8;
    oldcomvec = _dos_getvect(comvec);
    _dos_setvect(comvec, hasfifo ? com_isr_fifo : com_isr);
    outp(0x21, inp(0x21) & ~(1 << comirq));
    disable_ints();
    outp(comport + 1, 7);
    outp(0x20, 0xc2);
    enable_ints();
}

void com_dtr_off(void)
{
    outp(comport + 4, inp(comport + 4) & ~1);
}

void com_dtr_on(void)
{
    outp(comport + 4, inp(comport + 4) | 1);
}

void com_close(void)
{
    outp(comport + 1, 0);
    outp(comport + 4, 0);
    outp(comport + 2, 0);
    outp(0x21, inp(0x21) | (1 << comirq));
    _dos_setvect(comvec, oldcomvec);
    if (comdelay)
        lib_40ff0(comtask, 1);
}

short com_getc(void)
{
    short c;

    if (rxcount == 0)
        return -1;
    c = rxbuf[rxtail++];
    rxcount--;
    if (rxtail >= 0x4000)
        rxtail = 0;
    return c;
}

void com_putc(char c)
{
    short out;

    txbuf[txhead++] = c;
    txcount++;
    if (txhead >= 0x4000)
        txhead = 0;
    if ((inp(comport + 5) & 0x40) && txcount != 0) {
        out = txbuf[txtail++];
        txcount--;
        if (txtail >= 0x4000)
            txtail = 0;
        outp(comport, out);
    }
    if (comdelay) {
        comticks = 0;
        while (comticks < comdelay)
            ;
    }
}

/* differs: the transmit byte is in ax, the game's in dx (types of `out` tried: short, unsigned short, int, char) */
void __interrupt __far com_isr(void)
{
    short out;

    for (;;) {
        switch (inp(comport + 2) & 7) {
        case 4:
            rxints++;
            rxbuf[rxhead++] = inp(comport);
            rxcount++;
            if (rxhead >= 0x4000)
                rxhead = 0;
            break;
        case 2:
            txints++;
            if (txcount != 0) {
                out = txbuf[txtail++];
                txcount--;
                if (txtail >= 0x4000)
                    txtail = 0;
                outp(comport, out);
            }
            break;
        case 6:
            lsr = inp(comport + 5);
            if (lsr & 2)
                overruns++;
            if (lsr & 4)
                parityerrs++;
            if (lsr & 8)
                framingerrs++;
            if (lsr & 0x10)
                breaks++;
            break;
        default:
            goto done;
        }
    }
done:
    outp(0x20, 0x20);
}

/* differs: the receive store evaluates the index before inp(); the game reads the port first */
void __interrupt __far com_isr_fifo(void)
{
    short out;
    short n;

    for (;;) {
        switch (inp(comport + 2) & 7) {
        case 4:
            rxints++;
            do {
                rxbuf[rxhead++] = inp(comport);
                rxcount++;
                if (rxhead >= 0x4000)
                    rxhead = 0;
            } while (inp(comport + 5) & 1);
            break;
        case 2:
            txints++;
            if (txcount != 0) {
                n = 16;
                do {
                    out = txbuf[txtail++];
                    txcount--;
                    if (txtail >= 0x4000)
                        txtail = 0;
                    outp(comport, out);
                    if (--n == 0)
                        break;
                } while (txcount != 0);
            }
            break;
        case 6:
            lsr = inp(comport + 5);
            if (lsr & 2)
                overruns++;
            if (lsr & 4)
                parityerrs++;
            if (lsr & 8)
                framingerrs++;
            if (lsr & 0x10)
                breaks++;
            break;
        default:
            outp(0x20, 0x20);
            return;
        }
    }
}

void com_kick(void)
{
    short out;

    if (txcount != 0) {
        out = txbuf[txtail++];
        txcount--;
        if (txtail >= 0x4000)
            txtail = 0;
        outp(comport, out);
    }
}

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

int com_waitfor(char *s)
{
    char line[80];
    int n;
    int c;

    sendfail = 0;
    for (;;) {
        n = 0;
        for (;;) {
            if (aborted == 1) {
                sendfail = 1;
                return 0;
            }
            c = com_getc();
            if (c != -1) {
                if (c == 10 || n == 79)
                    break;
                if (c >= ' ')
                    line[n++] = c;
            }
        }
        line[n] = 0;
        if (strncmp(line, s, strlen(s)) == 0)
            return 1;
    }
}

int xalloc(void **p, int size)
{
    void *r;

    r = malloc(size);
    if (r == NULL)
        return -1;
    *p = r;
    return 0;
}

int xfree(void *p)
{
    if (p == NULL)
        return -1;
    free(p);
    return 0;
}

extern short v7ee5a;
extern int v7eca8, v7ea58;
extern int comopen;

struct task {
    struct task *next;
    struct task *prev;
    void (*service)(struct task *);
    void *data;
};

void get_7ee5a(void)
{
    v7eca8 = v7ee5a;
}

void set_7ee5a(void)
{
    v7ee5a = v7eca8;
    v7ea58 = v7eca8;
}

void com_shutdown(void)
{
    if (comopen) {
        com_close();
        comopen = 0;
    }
}

void tick_task(struct task *t)
{
    ++*(int *)t->data;
}

/* ---- sound effects (Apogee Sound System FX_ calls) */

struct sfxchan {
    int active;
    int handle;
    int plays;
    int pitch;
};

struct car {
    int sndhandle;
    char rest[0x100];
};

extern int soundon;
extern struct sfxchan sfxchans[];
extern char *sfxdata;
extern char *enginedata;
extern int sfxvol;
extern int enginevol;
extern short twoplayer, splitscreen;
extern struct car cars[2];

int FX_SoundActive(int handle);
int FX_StopSound(int handle);
int FX_PlayVOC(char *ptr, int pitchoffset, int vol, int left, int right, int priority, unsigned long callbackval);

void engine_stop(int i)
{
    if (soundon) {
        FX_StopSound(cars[i].sndhandle);
        cars[i].sndhandle = -1;
    }
}

extern char *sfx2data;

void sfx_play2(void)
{
    if (soundon)
        FX_PlayVOC(sfx2data, 0, sfxvol, sfxvol, sfxvol, 0x20, 0);
}

int sfx_busy(int ch)
{
    if (!soundon)
        return 0;
    if (!sfxchans[ch].active)
        return 0;
    if (FX_SoundActive(sfxchans[ch].handle))
        return 1;
    sfxchans[ch].active = 0;
    return 0;
}

void sfx_play(int ch)
{
    if (soundon && sfx_busy(ch) == 0) {
        sfxchans[ch].handle = FX_PlayVOC(sfxdata, sfxchans[ch].pitch, sfxvol, sfxvol, sfxvol, 0x16, 0);
        if (sfxchans[ch].handle >= 0) {
            sfxchans[ch].active = 1;
            sfxchans[ch].plays++;
        }
    }
}

void engine_clear(void)
{
    cars[0].sndhandle = -1;
    cars[1].sndhandle = -1;
}

void engine_start(void)
{
    int n;
    int i;
    int h;

    if (soundon) {
        n = (twoplayer || splitscreen) ? 1 : 0;
        for (i = 0; i <= n; i++) {
            h = FX_PlayVOC(enginedata, -1800, enginevol, enginevol, enginevol, 0x28, 0);
            if (h > 0)
                cars[i].sndhandle = h;
        }
    }
}

extern char pathext[];

void makepath(char *dst, char *src)
{
    strcpy(dst, src);
    strcat(dst, pathext);
}
