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
#include <stdio.h>

/* Game globals the source addresses by number (the game has an LE fixup at each use, so they
 * must be symbols: a bare constant is not relocated by DOS/4GW and writes to low memory -
 * found by the hybrid link, fn_17928 hung the race load). tools/link/symmap.py maps litNNNNN. */
extern char lit600a0[], lit6a4b8[], lit6aa68[], lit72350[], lit72354[], lit72356[], lit7b238[], lit7b378[], lit7d770[], lit7d7b8[], lit7d9a0[], lit7eb6c[], lit80ace[], lit6aab6[], lit6e728[], lit70660[], lit7cbe8[];

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
void TS_SetTaskRate(int, int);

/* differs (11 rows, 1 = the masked `[ecx*2 +/- @]` fixup): iter 39 made `rate` one unsigned variable
 * for the rate and the divisor (the game compares ECX = rate, not baud, with 0x3840 at 0x1039a and puts
 * the quotient back in ECX 0x103ba) and passes it to outp uncast: 13 -> 11. Left: the divisor byte
 * (`movzx eax,cl` 0x103d4 vs our `mov al,cl`), the MCR extend (`cwde` 0x1045d vs our `movzx eax,al`),
 * _dos_setvect's arg order (`movsx eax,[comvec]` 0x104ae after the isr choice, ours before) and the
 * IMR mask `1 << comirq` in EBX instead of EAX (no `mov ecx,eax` copy 0x104ca), plus one trailing nop.
 * Tried iter 39 (all worse or equal): `(unsigned char)rate`, `rate & 0xff`, `int`/`unsigned long`/
 * `unsigned short rate`, `short mcr`, `mcr |= 0x0b`, `iir` reused for the MCR (24), an isr pointer
 * local, `~(1 << comirq) & inp(0x21)`, the mask in `rate`; permute.py 20 rounds x 30: no gain.
 * Before: `rate == 14400` alone (18), `baud == 14400L` and `signed char mcr` (no change), an
 * `int imask` local (23), and parameters (port, irq, comnum, baud): 4 slot rows disappear but INT 14h
 * DX and the divisor swap (link_open's fixups flag it - findings/serial.md). */
void com_open(short port, short irq, short baud, short comnum)
{
    unsigned rate;
    short iir;
    char mcr;

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
    rate = baud;
    int386(0x14, &comregs, &comregs);
    if (rate == 14400)
        rate = 19200;
    rate = 1843200UL / (rate * 16);
    outp(comport + 3, 0x80);
    outp(comport + 1, 0);
    outp(comport, rate);
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
    mcr = inp(comport + 4) & 0xe4;
    outp(comport + 4, mcr | 0x0b);
    inp(comport);
    inp(comport + 2);
    comvec = comirq + 8;
    oldcomvec = _dos_getvect(comvec);
    _dos_setvect(comvec, !hasfifo ? com_isr : com_isr_fifo);
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
        TS_SetTaskRate(comtask, 1);
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

short com_getc_x(void);               /* com_getc under another name, for the call sites the game does not
                                       * inline: -oe cannot inline a body it does not see */
#pragma aux com_getc_x "com_getc_"

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

void __interrupt __far com_isr(void)
{
    short b;

    for (;;) {
        switch (inp(comport + 2) & 7) {
        case 4:
            rxints++;
            b = inp(comport);
            rxbuf[rxhead++] = b;
            rxcount++;
            if (rxhead >= 0x4000)
                rxhead = 0;
            break;
        case 2:
            txints++;
            if (txcount != 0) {
                b = txbuf[txtail++];
                txcount--;
                if (txtail >= 0x4000)
                    txtail = 0;
                outp(comport, b);
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

void __interrupt __far com_isr_fifo(void)
{
    short b;
    short n;

    for (;;) {
        switch (inp(comport + 2) & 7) {
        case 4:
            rxints++;
            do {
                b = inp(comport);
                rxbuf[rxhead++] = b;
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
                    b = txbuf[txtail++];
                    txcount--;
                    if (txtail >= 0x4000)
                        txtail = 0;
                    outp(comport, b);
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

/* 0x104-byte records; the base 0x68420 is a guess (lowest field seen so far: 0x68426) */
struct car {
    short f00, f02, f04;        /* 0x02/0x04 cleared per race (shots_clear 0x2d4fc) */
    short face;
    char f08[0x1a - 8];
    int score;
    int score2;
    char f22[0x5a - 0x22];
    short f5a;                  /* 0x5a: cleared with .face per race (fn_1dac0, fn_1832c 0x1839c) */
    char f5c[0x82 - 0x5c];
    int f82;
    char f86[0x94 - 0x86];
    short f94, f96, f98, f9a;
    short place;
    short lastcell;
    short backward;
    short lap;
    char fa4[0xb0 - 0xa4];
    int fb0, fb4, fb8, fbc;     /* 0xb0-0xbc: the second banner, as fd4/fe0/fe4/fe8 (overlay_draw) */
    int fc0;                    /* 0xc0: v684e0 / v685e4 for cars 0 and 1 */
    int sndhandle;
    int fc8, fcc;               /* 0xc8/0xcc: that banner's sound handle and playing flag */
    int fd0;
    int fd4;
    int fd8;
    int fdc;
    int fe0;
    int fe4;
    int fe8;
    int fec;
    int ff0;
    char ff4[0x104 - 0xf4];
};

extern int soundon;
extern struct sfxchan sfxchans[];
extern char *sfxdata;
extern char *enginedata;
extern int sfxvol;
extern int enginevol;
extern short twoplayer, splitscreen;
extern struct car cars[8];

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

/* ---- HUD and modem dialogue */

extern short spr_h, spr_w, spr_x, spr_y;
extern short playing[2];
extern char *faceimgs[];
extern char *sprptr;
extern int p2offset;        /* dword stores at 0x1799c/0x179ba */
void spr_draw(void);

void draw_faces(void)
{
    int i;

    spr_h = 26;
    spr_w = 21;
    spr_x = 293;
    spr_y = 178;
    for (i = 0; i <= 1; i++) {
        if (playing[i] && cars[i].face) {
            sprptr = faceimgs[cars[i].face];
            if (i == 1)
                spr_y -= p2offset;
            spr_draw();
        }
    }
}

extern unsigned short page_draw, page_from, page_to, page_shown;
extern unsigned short save_a, save_b, save_c;
extern int connected;
extern int ticks2;
extern char modeminit[];
extern char modemhangup[];
void page_copy(void);
void msgbox(char *s, int wait);
void fn_1c068(void);
void fn_1c0ac(void);

/* The game calls com_getc in here (0x10ee6, 0x1100a) while com_waitfor inlines it: com_getc_x. */
int modem(int hangup)
{
    char msg[40];
    int ok;

    save_a = page_draw;
    save_b = page_from;
    save_c = page_to;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    if (hangup == 0)
        strcpy(msg, "INITIALIZING MODEM");
    else
        strcpy(msg, "DISCONNECTING");
    msgbox(msg, 0);
    connected = 0;
    sendfail = 0;
    while (com_getc_x() != -1)
        ;
    sendfail = 0;
    if (hangup) {
        com_dtr_off();
        ticks = 0;
        ticks2 = 0;
        do {
            if (aborted == 1 && connected) {
                sendfail = 1;
                break;
            }
        } while (ticks < 0x110);
        com_dtr_on();
        sendstr("+++");
        ticks = 0;
        ticks2 = 0;
        do {
            if (aborted == 1 && connected) {
                sendfail = 1;
                break;
            }
        } while (ticks < 0x110);
        sendstr(modemhangup);
        ticks = 0;
        ticks2 = 0;
        do {
            if (aborted == 1 && connected) {
                sendfail = 1;
                break;
            }
        } while (ticks < 0x110);
        ok = 1;
        for (;;) {
            if (com_getc_x() == -1)
                break;
        }
    } else {
        sendstr(modeminit);
        ok = com_waitfor("OK");
    }
    page_from = 48000;
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
    if (!ok) {
        fn_1c068();
        msgbox("MODEM NOT RESPONDING", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        return 0;
    }
    connected = 1;
    return 1;
}

extern short netgame, net88c22, net88c14;
extern short musicon;
extern int musicpaused;
extern int linktype;
extern int net7ed78, net7ed64, net7ed68;
extern char netstats[5][60];
extern int badcrc, timeouts, noise;
void music_pause(void);
void music_resume(void);
int showlines();                      /* game: ECX is arg4 and the return (pragma aux below) */
#pragma aux showlines value [ecx] parm [eax] [edx] [ebx] [ecx] modify []

void net_end(int ok)
{
    char buf[20];
    unsigned len;
    int i;

    if (!netgame)
        return;
    if (musicon && !musicpaused) {
        music_pause();
        fn_1c068();
        msgbox("MUSIC PAUSED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        musicpaused = 1;
    }
    if (ok == 0) {
        fn_1c068();
        msgbox("CONNECTION TERMINATED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
    }
    if (linktype == 1)
        modem(1);
    com_shutdown();
    net88c22 = 0;
    netgame = 0;
    net7ed78 = 0;
    cars[0].fd0 = net7ed64;
    net88c14 = 0;
    cars[1].fd0 = net7ed68;
    if (musicpaused) {
        musicpaused = 0;
        music_resume();
    }
    strcpy(netstats[1], "BAD CRC  =");
    strcpy(netstats[2], "TIMEOUTS =");
    strcpy(netstats[3], "OVERRUNS =");
    strcpy(netstats[4], "NOISE    =");
    itoa(badcrc, buf, 10);
    strcat(netstats[1], buf);
    itoa(timeouts, buf, 10);
    strcat(netstats[2], buf);
    itoa(overruns, buf, 10);
    strcat(netstats[3], buf);
    itoa(noise, buf, 10);
    strcat(netstats[4], buf);
    len = strlen(netstats[1]);
    for (i = 1; i < 5; i++)
        if (strlen(netstats[i]) > len)
            len = strlen(netstats[i]);
    for (i = 1; i < 5; i++)
        while (strlen(netstats[i]) < len)
            strcat(netstats[i], " ");
    showlines(4, 1, 0);
}

/* ---- drawing helpers */

extern int clipy;
extern volatile char shotkey;
void spr_draw_clip(void);
void page_show(void);
void screenshot(void);

/* differs: 3 rows - the slot reload lands in EAX and the spr_y load in EBX, the game the other way
 * round (0x11416/0x11419); declaration orders of h/k/last/c, `k = last; k = k - 1;` and `k + spr_y`
 * tried (GOTCHAS "two locals' register pairing"). The frame is the game's: a `volatile int last`
 * written once gives `sub esp,4` + `mov [esp],eax` + `mov ebx,[esp]`, and `k = last - 1` as its own
 * statement puts the `dec` in the reloaded register (16 rows -> 3). */
void drawsprite_clip(void)
{
    short h;
    volatile int last;
    int k;
    short c;

    h = spr_w;
    last = h;
    k = last - 1;
    clipy = 0;
    if (spr_y + k > 199) {
        c = (short)(h - 1 + spr_y - 199);
        clipy = c;
        spr_w = h - c;
    }
    if (spr_w > 0)
        spr_draw_clip();
    clipy = 0;
    spr_w = h;
}

void frame_wait(void)
{
    page_show();
    if (shotkey) {
        screenshot();
        while (shotkey)
            ;
    }
}

void pal_shift(unsigned char *p)
{
    short i;
    short v;

    for (i = 1; i <= 768; i++)
        {
        v = *p;
        v <<= 2;
        *p++ = v;
    }
}

/* ---- screenshot (WACKnnn.PCX) */

struct pcxhdr {
    char manufacturer;
    char version;
    char encoding;
    char bpp;
    short xmin, ymin, xmax, ymax;
    short hres, vres;
    char pal16[48];
    char reserved;
    char nplanes;
    short bytesperline;
    short palinfo;
    char filler[58];
};

extern unsigned char *palptr;
void getpalette(void);
void spr_grab(void);

/* differs (6 rows, all the pal/lineptr stack-slot pair: game pal [esp+0x50c], lineptr [esp+0x508];
   ours the reverse - the slot-permutation dead end; declaration order (3), assignment order (3) and
   dropping either local (worse: 25-30) tried). Fixed in iter 37 (51 -> 6): int n read back as
   (short)n for itoa (the game's dword slot + movsx word reload), hp = &hdr with the other pointer
   inits (ebp from the prologue), the palette shift through its own pointer q in the pal_shift form
   (pal itself never moves, so `pal = palptr` went), and fputc(line[x++], f) (inc before the call).
   Iter 54: `volatile unsigned char *pl[2]` (pl[0] = line, pl[1] = palbuf) - 11 rows: the two elements
   are still coloured as separate pseudos and land on [esp+0x508]/[esp+0x510], with n pushed to
   0x50c; adding `volatile int n` made it worse (23, size 840). The volatile-array lever (wtext) only
   works when the permuted locals are the function's ONLY spilled ones. */
void screenshot(void)
{
    unsigned char palbuf[800];
    unsigned char line[324];
    struct pcxhdr hdr;
    char name[20];
    char digits[16];
    unsigned char *lineptr;
    unsigned char *pal;
    unsigned char *q;
    int n;
    short len, j;
    short i;
    short v;
    short x, run;
    short y;
    char *hp;
    FILE *f;

    pal = palbuf;
    lineptr = line;
    hp = (char *)&hdr;
    n = 1;
    sound(40);
    for (;;) {
        strcpy(name, "WACK");
        itoa((short)n, line, 10);
        strcpy(digits, "");
        strcpy(digits, "000");
        j = 2;
        for (len = strlen(line); len >= 1; len--)
            digits[j--] = line[len - 1];
        strcat(name, digits);
        strcat(name, ".PCX");
        f = fopen(name, "rb");
        if (f == NULL)
            break;
        fclose(f);
        n++;
    }
    palptr = pal;
    getpalette();
    q = pal;
    for (i = 1; i <= 768; i++) {
        v = *q;
        v <<= 2;
        *q++ = v;
    }
    for (i = 1; i <= sizeof(hdr); i++)
        *hp++ = 0;
    hdr.manufacturer = 10;
    hdr.version = 5;
    hdr.encoding = 1;
    hdr.bpp = 8;
    hdr.xmin = 0;
    hdr.ymin = 0;
    hdr.xmax = 319;
    hdr.ymax = 199;
    hdr.hres = 320;
    hdr.vres = 200;
    hdr.nplanes = 1;
    hdr.bytesperline = 320;
    hdr.palinfo = 0;
    f = fopen(name, "wb");
    fwrite(&hdr, sizeof(hdr), 1, f);
    for (y = 0; y <= 199; y++) {
        spr_x = 0;
        spr_h = 320;
        spr_w = 1;
        spr_y = y;
        sprptr = lineptr;
        spr_grab();
        x = 0;
        do {
            run = 0;
            while (line[x + run] == line[x + run + 1] && x + run < 320 && run < 63)
                run++;
            if (run > 0) {
                fputc(run | 0xc0, f);
                fputc(line[x], f);
                x += run;
            } else {
                if ((line[x] & 0xc0) == 0xc0)
                    fputc(0xc1, f);
                fputc(line[x++], f);
            }
        } while (x < 320);
    }
    fputc(12, f);
    fwrite(pal, 768, 1, f);
    fclose(f);
    nosound();
}

/* ---- music (Apogee Sound System MUSIC_ calls) */

extern short music88bcc;
extern int cursong;
extern int menusong;
int MUSIC_SongPlaying(void);
int MUSIC_StopSong(void);
int MUSIC_PlaySong(int song, int loop);
char *MUSIC_ErrorString(int code);

void music_play(int loop)
{
    union REGS r;

    if (MUSIC_PlaySong(cursong, loop)) {
        r.h.ah = 0;
        r.h.al = 3;
        int386(0x10, &r, &r);
        printf("Error - %s\n", MUSIC_ErrorString(-1));
        exit(1);
    }
}

void music_start(int song, int loop)
{
    music88bcc = 0;
    cursong = song;
    if (musicon) {
        if (MUSIC_SongPlaying())
            MUSIC_StopSong();
        music_play(loop);
    }
}

/* differs: the game loads the inlined loop argument (edx) before the stores; an explicit copy does not help.
 * Every inlined music_start(x, 0) in the game does this (0x11891, 0x2b6a3, 0x2cae2, 0x2e735) while an
 * inlined music_start(x, 1) keeps mov edx,1 early in ours too, so -oe only sinks the zero constant.
 * Tried: int/short/register local for the argument, local used by both calls, second (unused) parameter,
 * spelling the inline out by hand, swapping the two stores in music_start, #pragma aux parm [eax] [edx],
 * for(;;) wait loop - all produced the same 116 bytes with xor edx,edx after the cursong store. */
void music_jingle(int song)
{
    music_start(song, 0);
    while (MUSIC_SongPlaying())
        ;
    music_start(menusong, 1);
}

extern char key88cd5, key88cdd, key88cd6, key88cd3;
extern short joy88b40, joy88b42, joy88b44, joy88b46;
extern char key88cc3, key88ccc, key88cc4, key88cc5, key88cc6, key88cc7;

void input_clear1(void)
{
    key88cd5 = 0;
    key88cdd = 0;
    key88cd6 = 0;
    key88cd3 = 0;
    joy88b40 = 0;
    joy88b42 = 0;
    joy88b44 = 0;
    joy88b46 = 0;
}

void input_clear2(void)
{
    key88cc3 = 0;
    key88ccc = 0;
    key88cc4 = 0;
    key88cc5 = 0;
    key88cc6 = 0;
    key88cc7 = 0;
}

/* ---- configuration file */

extern int ctl775cc, ctl77558, ctl774e4;
extern short ctl775d8, ctl775dc, ctl775d6, ctl775da;
extern int v7ecb4;
extern char cfgname[];
extern char cfg7ee1c[0x492];
extern short cfg7ee2a;
extern char cfg7b6d8[0x18];
extern char cfg77128[0x630];
void waitkey(void);
void waitkey_x(void);   /* called, not inlined: the game calls 0x341d8 */
#pragma aux waitkey_x "waitkey_"
int save_config(short keep);

int save_config(short keep)
{
    FILE *f;
    union REGS r;

    f = fopen(cfgname, "wb");
    if (f == NULL) {
        r.h.ah = 0;
        r.h.al = 3;
        int386(0x10, &r, &r);
        printf("Cannot open %s for WRITE/UPDATE\n", cfgname);
        printf("If you are running this game from a CD\n");
        printf("You will need to set the APOGEECD enviroment label\n");
        printf("To a writeable area of your system.\n");
        printf("Example:  SET APOGEECD=C:\\WACKY\\ \n");
        waitkey_x();
        return 0;
    }
    if (keep && cfg7ee2a)
        cfg7ee2a = 0;
    fwrite(cfg7ee1c, 0x492, 1, f);
    fwrite(cfg7b6d8, 0x18, 1, f);
    fwrite(cfg77128, 0x630, 1, f);
    fclose(f);
    return 1;
}

int save_ctl(void)
{
    if (splitscreen) {
        ctl775cc = splitscreen;
        ctl775d8 = v7ee5a;
        ctl77558 = 1;
        ctl775dc = v7ecb4;
    } else {
        ctl775d6 = v7ee5a;
        ctl774e4 = 1;
        ctl775da = v7ecb4;
    }
    return save_config(0);
}

void flushkeys(void)
{
    while (kbhit())
        getch();
}

/* ---- sound voices */

struct voice {                  /* 0x7A9C8, 27 of them */
    char *data;
    int f04;
    int pitch;
    int priority;
    int handle;
    int plays;
};

extern short sfxon;
extern struct voice voices[27];
extern short voicebusy;
void FX_StopAllSounds(void);

/* differs: the -1/0 constants of the unrolled first loop live in EBX/EDX, the game's in EDX/EBX
 * (0x11b1e); the other 18 fndiff rows are masked-fixup artifacts. Tried: statement order in both
 * loops, int m = -1 / z = 0 locals (before or inside the loop, both declaration orders: the
 * constants propagate, same bytes), short i (worse). The coin-flip class (GOTCHAS dead ends).
 * Iter 94 vbatch (7 spellings: swap-stores, bound 0x1b, `i != 27`, while, do/while, the two loops
 * swapped, a `struct voice *p` walk): all 4 rows. The store order does not move a constant's home -
 * with `plays = 0` written first EBX still materialises -1 and EDX the 0 (.work/vbatch/V.OBJ), so
 * creation order here is not the source order of the stores. */
void voices_reset(void)
{
    int i;

    if (sfxon) {
        FX_StopAllSounds();
        for (i = 0; i < 27; i++) {
            voices[i].handle = -1;
            voices[i].plays = 0;
        }
        for (i = 0; i < 8; i++) {
            sfxchans[i].active = 0;
            voices[i].plays = 0;
        }
    }
}

void voice_check(int n)
{
    voicebusy = 0;
    if (sfxon && voices[n].handle != -1) {
        if (FX_SoundActive(voices[n].handle)) {
            voicebusy = 1;
            return;
        }
        voices[n].handle = -1;
    }
}

extern int netvoices;
extern short myplayer;
extern int countplays;

void voice_play(int n);

void voice_play(int n)
{
    char *data;                                 /* v unused: game callers pass 1 or 2 args (EDX) */
    int h;

    if (sfxon) {
        if (netvoices && n <= 7 && n != myplayer) {
            if (!soundon)
                return;
            data = sfx2data;
        } else {
            if (n == 0xFF)
                n = 3;
            data = voices[n].data;
        }
        h = FX_PlayVOC(data, voices[n].pitch, sfxvol, sfxvol, sfxvol, voices[n].priority, 0);
        if (h > 0) {
            voices[n].handle = h;
            if (countplays)
                voices[n].plays++;
        }
    }
}

/* ---- sound and music setup */

extern short joy_skip, sfxready, joyfound, v7ee4a, v7ee44;
extern int musicready, sfxsoft;
extern int musiccard, musicaddr, musicvol;
extern int fxcard, fxvoices, fxbits, fxvol;
extern char fxdevice[];
extern short joy_xbit, joy_ybit, joy_bits;
int MUSIC_Init(int card, int address);
char *MUSIC_ErrorString(int err);
void MUSIC_SetVolume(int vol);
int FX_SetupCard(int card, void *device);
int FX_Init(int card, int voices, int channels, int bits, unsigned rate);
char *FX_ErrorString(int err);
void FX_SetVolume(int vol);
void joy_poll(void);

int sound_init(void)
{
    int status;

    joy_skip = 0;
    soundon = 0;
    musicready = 0;
    sfxsoft = 0;
    sfxon = 0;
    sfxready = 0;
    musicon = 0;
    strcpy(pathext, "");
    if (musiccard != -1) {
        printf("Checking Music Hardware....\n");
        status = MUSIC_Init(musiccard, musicaddr);
        if (status != 0) {
            printf("%s\n", MUSIC_ErrorString(status));
            waitkey_x();
            return 0;
        }
        printf("OK\n");
        musicon = 1;
        if (musiccard <= 2)
            strcpy(pathext, ".KLM");
        else
            strcpy(pathext, ".MID");
        MUSIC_SetVolume(musicvol);
        musicready = 1;
    } else {
        printf("No MUSIC selected....\n");
    }
    if (fxcard != -1) {
        printf("Checking FX Hardware....\n");
        status = FX_SetupCard(fxcard, fxdevice);
        if (status != 0) {
            printf("%s\n", FX_ErrorString(status));
            waitkey_x();
            return 0;
        }
        status = FX_Init(fxcard, fxvoices, 1, fxbits, 11000);
        if (status != 0) {
            printf("%s\n", FX_ErrorString(status));
            waitkey_x();
            return 0;
        }
        sfxon = 1;
        if (fxcard != 7 && fxcard != 2)
            soundon = 1;
        printf("OK\n");
        FX_SetVolume(fxvol);
        sfxvol = fxvol;
        sfxsoft = soundon;
    } else {
        printf("No SOUND FX selected....\n");
    }
    joy_xbit = 1;
    joy_ybit = 2;
    joy_bits = 3;
    input_clear1();
    joyfound = 0;
    joy_skip = 0;
    joy_poll();
    input_clear1();
    if (joyfound) {
        v7ee4a = 0;
        ((short *)cfg77128)[0] = 0;
        ((short *)cfg77128)[1] = 0;
        ((short *)cfg77128)[2] = 1;
    }
    if (!v7ee4a)
        v7ee44 = 0;
    else if (v7ee44)
        return 1;
    joy_skip = 1;
    return 1;
}

/* ---- voice samples */

struct datent {                 /* 0x65080: the data file's directory */
    char name[14];
    long size;
    long offset;
};

extern struct datent datdir[];
extern short datidx;
extern FILE *datfile;
extern char *voicedata;
short datfind(char *name);
long datsize(void);
void readdat(void *buf, int n);

/* differs: first and the priority counter swap ebx/ecx in the first loop (+110); bytes otherwise equal.
 * Rows to subtract first (iter 33): 20 of the 33 are masked-fixup pairs - `[reg - 0x6f6f6f70]` on our
 * side is an unlinked relocation where the game has the resolved `[reg + 0x6508e]`-style literal
 * (0x11fb0, 0x11fe3, 0x12035, 0x1207c-0x120d9); the 13 real rows are the EBX/ECX colour of `first`
 * (game EBX 0x12005 `xor ebx,ebx`/0x12010 `mov ebx,0x1a`, ours ECX) and of the data temp + priority
 * counter (game ECX 0x12015 `mov ecx,0x1b`), i.e. the documented register-home dead end
 * (QUESTIONS.md 2) - SKIP at 0 variants, no new lever offered. */
void voices_load(void)
{
    long *offs;
    long size;
    int i, first;

    datfind("AUDIOHED.FX");
    size = datsize();
    offs = malloc(size);
    readdat(offs, size);
    datfind("AUDIOT.FX");
    size = datsize();
    voicedata = malloc(size);
    readdat(voicedata, size);
    first = 0;
    if (fxcard == 2)
        first = 26;
    for (i = 1; i < 27; i++) {
        voices[i - 1].data = voicedata + offs[first + i - 1];
        voices[i - 1].f04 = *(int *)voices[i - 1].data;
        voices[i - 1].handle = -1;
        voices[i - 1].pitch = 0;
        voices[i - 1].plays = 0;
        voices[i - 1].priority = 28 - i;
    }
    first = 20;
    for (i = 0; i < 8; i++) {
        voices[i].data = voices[first].data;
        voices[i].f04 = voices[first].f04;
    }
    voices[26].data = voices[18].data;
    voices[26].f04 = voices[18].f04;
    voices[26].handle = voices[18].handle;
    voices[26].priority = voices[18].priority;
    voices[26].pitch = 0;
    free(offs);
}

extern char *gigdata;
extern char *gigs[7];

/* The second loop's name pointer is the explicit `nm`, declared before i/pri and set after the
   MOTOR.VOC read: with `names[i]` the compiler's own pointer took pri's slot (0x168/0x16C swapped). */
void sounds_load(void)
{
    char names[27][12] = {
        "UNO", "SULTAN", "MORRIS", "PEGGLES", "RAZER", "RINGO", "BLOMBO", "TIGI", "PASS",
        "BELL", "START", "GLASS", "WARP", "BOOM", "HORN", "SPLASH", "PLIP", "SKID", "PUF",
        "FLAME", "HOG", "CLANG", "SWIPE", "DROP", "BACKFIRE", "STALL1", "NO"
    };
    char fname[16];
    char num[16];
    char *p, *q;
    short len;
    char *nm;
    int i, pri;

    if (!sfxon)
        return;
    if (fxcard == 7 || fxcard == 2) {
        voices_load();
        return;
    }
    pri = 27;
    p = gigdata;
    for (i = 0; i <= 4; i++) {
        itoa(i + 1, num, 10);
        strcpy(fname, "GIG");
        strcat(fname, num);
        strcat(fname, ".VOC");
        datfind(fname);
        len = datsize();
        readdat(p, len);
        gigs[i] = p;
        p += len;
    }
    gigs[6] = gigs[5] = gigs[4];
    q = voicedata;
    datfind("MOTOR.VOC");
    readdat(enginedata, 0x942);
    nm = names[0];
    for (i = 0; i < 27; i++) {
        strcpy(fname, nm);
        strcat(fname, ".VOC");
        datfind(fname);
        len = datsize();
        readdat(q, len);
        voices[i].data = q;
        voices[i].handle = -1;
        voices[i].pitch = 0;
        voices[i].plays = 0;
        voices[i].priority = pri--;
        q += len;
        nm += 12;
    }
    voices[25].priority = 0x20;
    voices[26].priority = voices[18].priority;
    datfind("HORN1.VOC");
    readdat(sfxdata, 0xFE5);
    for (i = 0; i < 9; i++) {
        sfxchans[i].active = 0;
        sfxchans[i].plays = 0;
    }
    sfxchans[0].pitch = -650;
    sfxchans[1].pitch = -300;
    sfxchans[2].pitch = 200;
    sfxchans[3].pitch = 750;
    sfxchans[4].pitch = 0;
    sfxchans[5].pitch = -300;
    sfxchans[6].pitch = 750;
    sfxchans[7].pitch = 200;
}

/* ---- timers, error log */

int since2(int t)
{
    return ticks2 - t;
}

int since(int t)
{
    return ticks - t;
}

extern FILE *errlog;

void logerr(char *s)
{
    fprintf(errlog, "%s\n", s);
    fclose(errlog);
    errlog = fopen("ERR.LOG", "at+");
}

/* ---- per-track values */

extern unsigned track;
extern int trk7ebc8, trk7ec44, trk7ebc4, trk7ec40;

/* differs: one moved load - the game loads x (ebx) at +8 and the switch subject (eax) at +14, ours
 * hoists the switch subject above the x load. Tried x/y assignment order, `int y, x;` declaration order
 * (that changes the register choice, 13 diffs), `register`, `unsigned`, switching on a local copy, and
 * compiling with -3 instead of -3r (identical object): all give these same 212 bytes. Iter 49: permute.py
 * 4 rounds x 30 copies (seed 11) - best 2 = control, no mutation sinks the `mov eax,[track]`. */
void track_setup(void)
{
    int x, y;

    y = trk7ec44;
    x = trk7ebc8;
    switch (track) {
    case 3:
    case 4:
    case 9:
        trk7ebc4 = 0x528;
        trk7ec40 = 0x618;
        x = 0x528;
        y = 0x618;
        break;
    case 6:
    case 10:
    case 13:
    case 15:
    case 25:
        x = 0x348;
        y = 0x438;
        break;
    case 24:
    case 29:
    case 35:
        x = 0x528;
        y = 0x618;
        break;
    }
    trk7ebc8 = x;
    trk7ec44 = y;
}

/* ---- zone test, map cells */

#pragma pack(1)
struct surf {                   /* 0x741f8, 0x12e bytes per surface class surf_class (kart_move) */
    char k1[0x80];              /* per track [track - 1]: 3 = keep the kart's last animation (kart_draw) */
    char k2[0x80];              /* the same for the second animation */
    int f100, f104;             /* animation ids = their offsets in EFFECTS.SP (effects_load) */
    short f108, f10a, f10c, f10e;   /* f108 frame size, */
    short f110, f112, f114, f116, f118, f11a, f11c, f11e;   /* f10a/f10c h/w, f112 frames; f114-f11e the second */
    int f120;                   /* -> kart f36 */
    short f124;                 /* -> kart f3a */
    int f126, f12a;             /* -> kart f60, f64 */
};
struct kart {                   /* karts[] 0x68410, 0x104 bytes: the same records as cars[] (0x68420), seen
                                 * 0x10 earlier (cars[].sndhandle +0xc4 = +0xd4 here); curkart points in */
    short f00, f02, f04;
    short f06;                  /* 0x06: 1 or 2 = short spin, counted by f08 (kart_state) */
    short f08;
    short f0a;
    short f0c, f0e, f10;        /* 0x0C-0x10: set to 8, 0x28, 8 by kart_hit 0x216a0 */
    short f12, f14, f16, f18, f1a, f1c, f1e, f20, f22, f24;
    short f26;                  /* 0x26: speed, decays by f36 (16.16) each call (kart_state) */
    short f28, f2a, f2c;
    int f2e;                    /* 0x2E: race progress, compared with karts[0] (kart_catchup) */
    int f32;                    /* 0x32: ticks2 stamp (shot_hit) */
    int f36;
    short f3a, f3c, f3e, f40;   /* 0x3A-0x40: charge threshold, phase, doubled threshold, countdown */
    short f42, f44;
    short f46, f48;             /* 0x46: spark kind by side (kart_bump) */
    char *f4a;                  /* 0x4A: sparkdata (kart_bump) */
    int *f4e;                   /* 0x4E: &tb7b448[f1a][player] (shot_hit) */
    short f52, f54;             /* 0x52/0x54: drift counters left/right, 0..4 or 0x10 in a spin (kart_move) */
    int f56;                    /* 0x56: surface class surf_class under the kart (kart_move) */
    short f5a, f5c, f5e;
    int f60, f64;               /* 0x60/0x64: from the surface record +0x126/+0x12a (kart_move) */
    short f68;                  /* 0x68: toggles each frame while driving (self_project bob) */
    short f6a;                  /* 0x6A: counts up by 4 per pickup, at most 99 (kart_bump) */
    short f6c;
    int f6e;                    /* 0x6E: id of the surface animation playing (kart_draw) */
    struct surf *f72;           /* 0x72: its surface record */
    struct surf *f76;           /* 0x76: &surfs[f56] (kart_move) */
    short f7a;                  /* 0x7A: its frame (kart_draw) */
    short f7c;                  /* 0x7C: set to 1 on touching an entity or another player (kart_hit) */
    short f7e;                  /* 0x7E: set by a shot hit in a human-player race (shot_hit) */
    short f80;                  /* 0x80: fire held (kart_fire) */
    char f82[0x86 - 0x82];
    int f86;                    /* 0x86: -1 at race start (race_run) */
    int f8a;                    /* 0x8A: ticks2 stamp of f14 */
    int f8e;                    /* 0x8E: ticks2 when fire was pressed (kart_fire) */
    char f92[0xa2 - 0x92];
    short fa2, fa4, fa6, fa8;   /* cleared at race start (race_run) */
    short faa;                  /* 0xAA: 1 when the move hit something or f3c was set (kart_move) */
    short fac;                  /* 0xAC: 1..3 picks the catch-up step (kart_catchup) */
    short fae;                  /* 0xAE: map cell under the kart (race_run) */
    short fb0, fb2;             /* 0xB2: laps done (race_run: nlaps + 1 ends the race) */
    short fb4;
    short fb6;                  /* 0xB6: set to 0x10 with fb4 (kart_boost) */
    int fb8, fbc, fc0, fc4, fc8, fcc, fd0;  /* 0xC0-0xCC = cars[].fb0-fbc (second banner) */
    int fd4;                    /* 0xD4: = cars[].sndhandle */
    int fd8, fdc;               /* = cars[].fc8 / fcc: banner sound handle, playing */
    unsigned char fe0;          /* 0xE0: bits 0x10 / 8 pick weapon 3 / 8 when f16 is 0 (kart_fire) */
    char fe1[0xe4 - 0xe1];
    int fe4, fe8, fec;
    char ff0[0x100 - 0xf0];
    int f100;
};
#pragma pack()
extern struct kart karts[];

extern struct kart *curkart;
extern int curpos;

int in_zone(void)
{
    int r = 0;

    if (curkart->fb4) {
        if (curpos >= trk7ebc8 && curpos <= trk7ec44)
            r = 1;
    } else if (curkart->f0a) {
        if (curpos >= trk7ebc4 && curpos <= trk7ec40)
            r = 1;
    }
    return r;
}

extern short *map;
extern short *mapp;

short map_at(short x, short y)
{
    x -= 0x400;
    y -= 0x400;
    if (x < 0 || x > 0x7FF || y < 0 || y > 0x7FF)
        return 0;
    mapp = map + (y >> 5) * 64 + (x >> 5);
    return *mapp;
}

int map_cell(short x, short y)
{
    return (y >> 5) * 64 + (x >> 5);
}

/* ---- swapping the second player's view in and out */

extern short view_x, view_y, v88c6e, v88c72, v88c76, v88ca2, v88ca6, v88c6a, v88b7e;
extern int v7d898, v7d860, v7d840;

#pragma pack(1)                 /* the game has int f5e at +0x5E: pack(1) confirmed */
struct player {                 /* 0x68 bytes; base 0x6AA90 (CONFIRMED by menu sprptr code) */
    short f00, f02, f04, f06, f08, f0a, f0c, f0e;
    short f10, f12, f14;
    short x, y;                 /* 0x16, 0x18 */
    short f1a;                  /* 0x1A: set from [0x72356], then fn_16d68 table writes */
    short f1c, f1e;
    unsigned short place;       /* 0x20 */
    short lap, cell, face;      /* 0x22, 0x24, 0x26 */
    short f28[2], f2c[2], f30[2], f34[2];   /* 0x28-0x37: per viewing player [v88b7e] (kart_bump) */
    short f38[2];               /* 0x38: per-player values, [v88b7e] (shot_hit) */
    int f3c[2];                 /* 0x3C: ticks2 stamps (kart_bump) */
    char f44[0x4c - 0x44];
    int f4c;                    /* 0x4C: ticks2 stamp of f58 (shot_hit) */
    int pos;                    /* 0x50 */
    short f54, f56, f58, f5a, f5c;
    int f5e;                    /* 0x5E: animation cursor (fn_16d68) */
    short f62, f64, f66;
};
#pragma pack()

/* raw short/int at an absolute address (Watcom folds [reg+disp] like the game) */
#define WS(a) (*(short *)((size_t)(size_t)(a)))
#define WU(a) (*(unsigned short *)((size_t)(size_t)(a)))
#define WI(a) (*(int *)((size_t)(size_t)(a)))

extern struct player players[];
extern struct kart *kart7d5bc, *kart7d5c0;

void view2_in(void)
{
    v88c6e = view_x;
    v88c72 = view_y;
    v88c76 = myplayer;
    v7d898 = v7d860;
    curkart = kart7d5bc;
    view_x = v88ca2;
    view_y = v88ca6;
    curpos = players[1].pos;
    myplayer = v88c6a;
    v7d860 = v7d840;
    v88b7e = 1;
}

void view2_save(void)
{
    v88ca2 = view_x;
    v88ca6 = view_y;
    players[1].pos = curpos;
    v7d840 = v7d860;
}

void view2_out(void)
{
    view_x = v88c6e;
    view_y = v88c72;
    curpos = players[0].pos;
    myplayer = v88c76;
    v7d860 = v7d898;
    curkart = kart7d5c0;
    v88b7e = 0;
}

/* ---- place display */

extern int v7d998;
extern short v88bb2;   /* signed: the game's loads are movsx (0x1ce7d, 0x1d120) */
struct face {                 /* 0x98-byte DAT record (0x73C08) */
    char *img[35];
    short f8c, f8e;           /* img tail words if unused */
    short f90, f92;           /* frame counts (0x73C98 / 0x73C9A) */
    int f94;                  /* record byte size (0x73C9C) */
};
extern struct face faces[];
void print_at(short x, int y, char *s, short mode);

void show_place(short n)
{
    char buf[4];
    unsigned short save;

    if (splitscreen || v7d998 || v88bb2 == 4)
        return;
    switch (players[n].place) {
    case 1:
        strcpy(buf, "1ST");
        break;
    case 2:
        strcpy(buf, "2ND");
        break;
    case 3:
        strcpy(buf, "3RD");
        break;
    }
    save = page_draw;
    spr_y = 4 + (players[n].place - 1) * 0x1C;
    page_draw = 0;
    print_at(0xF0, spr_y, buf, 0);
    sprptr = faces[players[n].face].img[0];
    spr_x = 0x10E;
    spr_y = 4 + (players[n].place - 1) * 0x1C;
    spr_h = 0x26;
    spr_w = 0x1C;
    spr_draw();
    page_draw = 16000;
    spr_draw();
    print_at(0xF0, spr_y, buf, 0);
    page_draw = save;
}

/* ---- race places */

extern short nplayers;

void calc_places(void)
{
    int idx[10];
    int score[10];
    int n;
    int i, j, t, found, s, pl;

    for (i = 1; i <= 8; i++) {
        score[i] = cars[i - 1].score;
        idx[i] = i - 1;
    }
    for (i = 1; i <= 7; i++) {
        for (j = i + 1; j <= 8; j++) {
            if (score[i] < score[j]) {
                t = score[i];
                score[i] = score[j];
                score[j] = t;
                t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
        }
    }
    if (splitscreen && nplayers > 1) {
        for (i = 0; i <= nplayers; i++) {
            if (idx[i + 1] != 0 && idx[i + 1] != 1)
                cars[idx[i + 1]].place = i + 1;
        }
    }
    for (i = 0; i <= nplayers; i++) {
        if (idx[i + 1] == 0) {
            j = i;
            n = i + 1;
            break;
        }
    }
    s = cars[0].score;
    found = 0;
    for (i = 0; i <= nplayers; i++) {
        if (i != j && cars[idx[i + 1]].score == s) {
            found = 1;
            break;
        }
    }
    if (!found) {
        if (v88bb2 == 4)
            n = 1;
        if (n < 1)
            n = 1;
        if (n > 8)
            n = 8;
        cars[0].place = n;
    }
    if (splitscreen) {
        for (i = 0; i <= nplayers; i++) {
            if (idx[i + 1] == 1) {
                j = i;
                n = i + 1;
                break;
            }
        }
        if (n < 1)
            n = 1;
        if (n > 8)
            n = 8;
        s = cars[1].score;
        found = 0;
        for (i = 0; i <= nplayers; i++) {
            if (i != j && cars[idx[i + 1]].score == s) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (v88bb2 == 4)
                n = 1;
            if (n < 1)
                n = 1;
            if (n > 8)
                n = 8;
            cars[1].place = n;
        }
    } else {
        j = 0;
    }
    for (i = 0; i <= j; i++) {
        pl = (short)players[i].place;
        if (pl) {
            if (pl < 1)
                pl = 1;
            if (pl > 8)
                pl = 8;
            if (v88bb2 == 4)
                pl = 1;
            cars[i].place = pl;
        }
    }
}

/* ---- laps */

extern short curcell, ncells, nlaps, finished;
extern int racetime, lastlap;
extern int laptimes[];
void FX_StopSound(int handle);
void car_finished(int i);

/* differs (65 rows, 63 of them the masked `[reg +/- @]` fixups; 2 real): iter 39 29 -> 2 real rows.
   The laptimes index is a block-local `int k` (the game's temp in EAX, value in EDX 0x12c0f, size
   back to the game's 1084), the backward score2 is `(lap - 1) * ncells` (the shared imul tail
   0x12e1e takes lap-1 in EDI), and the lap cap is written `nlaps + 1 > players[i].lap` (the game
   loads nlaps first 0x12ed0). Left: that compare's operand pair only - game `cmp edi,eax; jge`
   0x12ee0, ours `cmp eax,edi; jle`, same two loads, same `add eax,edx` 0x12ede. Twenty-one more
   spellings in one probe compile (.work/probe_cl2) never break it, and the rule explains why: 9.5b
   gives the source comparison's LEFT operand both the cmp destination and the first-loaded value's
   home, so the lap-left forms (`lap < nlaps + c`, `!(lap >= nlaps + 1)`, `lap < nlaps + 1`) emit the
   game's cmp and `jge` but swap the two movsx homes and the add (4 rows), while every sum-left form
   keeps the game's homes with our cmp direction (2). Also tried: `nlaps + c > lap`, `c + nlaps`,
   `lap - nlaps < c` (2 rows of `sub edi,eax` + `cmp edi,edx` instead), `c > lap - nlaps` (3),
   `lap - c < nlaps` (4), `nlaps + c >= lap + 1` and `lap + 1 <= nlaps + c` (12, 13), a local for the
   sum (4), `players[i].lap = players[i].lap + 1` (11), a `struct player *pp` for the row (35 rows,
   1073 bytes), `1 + nlaps`, casts either side, plus iter 47's `lap <= nlaps`/`nlaps >= lap` (21 each)
   and permute.py 20 rounds: dead end (GOTCHAS "coin flip"). The game's `add eax,edx` is no evidence
   its source added `c`: 9.5b proves c == 1 at 0x12eba and reuses EDX for our literal 1.
   The game inlines since2(cars[i].f82) (argument in EAX, ticks2 - t in EDX) where our -oe keeps
   the call: written out with a temp, `(t = cars[i].f82, ticks2 - t)`, that site matches. */
void car_laps(void)
{
    int n;
    int i;
    int c;
    int t;

    n = splitscreen != 0;
    for (i = 0; i <= n; i++) {
        curcell = map_at(players[i].x, players[i].y);
        if (curcell == 0)
            continue;
        if (curcell == 1 && cars[i].lastcell == ncells) {
            cars[i].f96 = 0;
            if (cars[i].backward) {
                cars[i].backward = 0;
            } else {
                cars[i].lap++;
                if (!twoplayer && !splitscreen && !v7d998 && v88bb2 == 4 && cars[i].lap > 1 && cars[i].lap <= 7) {
                    {
                        int k = cars[i].lap - 2;

                        laptimes[k] = k == 0 ? racetime : racetime - lastlap;
                    }
                    lastlap = racetime;
                }
                if (cars[i].lap <= nlaps) {
                    cars[i].f94 = 1;
                    if (cars[i].lap == nlaps) {
                        voice_check(9);
                        if (!voicebusy)
                            voice_play(9);
                        cars[i].fd4 = 7;
                        cars[i].fe0 = 0;
                        cars[i].fdc = 6;
                        cars[i].fe4 = 0;
                        cars[i].fe8 = 0;
                        if (soundon) {
                            if (cars[i].ff0) {
                                FX_StopSound(cars[i].fec);
                                cars[i].ff0 = 0;
                            }
                            car_finished(i);
                        }
                    }
                }
            }
        } else if (curcell == ncells && cars[i].lastcell == 1) {
            if (cars[i].f9a) {
                if (!cars[i].f96) {
                    cars[i].f98 = 0;
                    cars[i].f82 = ticks2;
                }
                cars[i].f96 = 1;
            }
            cars[i].backward = 1;
        } else if (curcell < cars[i].lastcell) {
            if (!cars[i].f9a) {
                if (!cars[i].f96) {
                    cars[i].f98 = 0;
                    cars[i].f82 = ticks2;
                }
                cars[i].f96 = 1;
            }
        } else if (cars[i].f96) {
            if (curcell > cars[i].lastcell || (t = cars[i].f82, ticks2 - t) >= 10)
                cars[i].f96 = 0;
        }
        cars[i].lastcell = curcell;
        if (!cars[i].backward) {
            cars[i].score = (cars[i].lap << 10) + curcell;
            cars[i].score2 = curcell + cars[i].lap * ncells;
        } else {
            cars[i].score = ((cars[i].lap - 1) << 10) + curcell;
            cars[i].score2 = curcell + (cars[i].lap - 1) * ncells;
        }
        if (cars[i].lap == nlaps + 1 && players[i].place == 0) {
            players[i].place = ++finished;
            if (finished <= 3)
                show_place(i);
        }
    }
    for (i = 0; i <= nplayers; i++) {
        if (i == 0)
            continue;
        if (splitscreen && i == 1)
            continue;
        c = map_at(players[i].x, players[i].y);
        if (c == 1 && players[i].cell == ncells) {
            if (nlaps + 1 > players[i].lap)
                players[i].lap++;
            if (players[i].lap == nlaps + 1 && players[i].place == 0) {
                players[i].place = ++finished;
                if (finished <= 3)
                    show_place(i);
            }
        }
        cars[i].score = (players[i].lap << 10) + c;
        cars[i].score2 = players[i].lap * ncells + c;
        players[i].cell = c;
    }
    calc_places();
}

/* ---- status bar */

extern char pnames[][12];
extern short v88ca0, v88bde, v7eb80, netgame;
extern int v7ec7c;
extern char *img7eb5c, *img7eb3c, *img7d83c, *img7eb60, *img7d814, *img7d810;
void spr_grab(void);

/* differs (17 rows, 1100 bytes vs the game's 1080, 243 insns vs 246): (int) casts on both arms of the
   v7eb80 ternary give the game's per-arm movsx (no cwde) and, as a side effect, the esi form (movsx
   esi; imul esi; add esi,$pnames) at the first v88c6a strcpy: 48 -> 30. Left: (a) the ternary strcpy
   and every later print_at - 9.5b hoists `mov ebx,esp` and `xor ecx,ecx` (print_at args) above the
   inline strcpy, the game issues them after `pop edi` (0x13103/0x13123, 0x13267/0x132ff); (b) the
   second netgame==1 if/else - the game builds v88c6a in the esi form in the first arm and myplayer in
   the eax form in the second (0x132ba, 0x132ce; no tail merge, one pnames ref more), 9.5b selects the
   index in each arm and shares one imul; the first pair (0x13218, 0x13237) matches byte for byte in
   both arms, so only the second merges. Tried: 7 index spellings (all canonicalise), strcpy
   before/between the spr stores, ternary as arms/an int or short local k (if/else and ?:),
   single-arm casts, != 0, (int)/(short)/+0 on the netgame sites, both if/else orders, a ternary there,
   spr_y = spr_y + 0x1E; permute.py 12 rounds. iter 46: `strcpy(buf, (char *)pnames + (int)v88c6a *
   12)` in the then arm (canonicalises, 17), and the pointer ternary `strcpy(buf, netgame == 1 ?
   pnames[v88c6a] : pnames[myplayer])` (17 -> 35: it also splits the FIRST pair's arms and sinks
   print_at's args). The game's duplicated pair is 9 bytes longer than our join (246 insns vs 243)
   while our OBJ span is 20 bytes longer overall; the last row is the game's `mov eax,eax` padding vs
   our nop.
   iter 64 BEHAV census (globrefs: `counts 0x7d228 ours x6 game x7`): SAME, it is cause (b). Both arms
   give the game's index (game 0x132ba `movsx esi,[v88c6a]` in the netgame==1 arm, 0x132ce
   `movsx eax,[myplayer]` in the else arm; ours 0x32aa / 0x32b3 into one shared `imul eax,eax,0xc`
   0x32ba + one base load 0x32bd), pnames is read here and nowhere written, and no store is missing.
   The `game only 0x60278 x2 / 0x60280 / 0x6028c` are the game's string literals - "SCORE" twice
   (0x13283, 0x13316), "DEMO MODE " 0x13384, "TIME TRIAL" 0x133c1 - against our four (0x3273, 0x32fb,
   0x3369, 0x33a6), same count, our literals are fixups globrefs skips (GOTCHAS). The one-sided branch
   signature is the same jump landing on our shared-index block 0x32ba rather than on the game's
   strcpy head 0x132e1 (that block uses no absolute address). No BUG, no code change. */
void draw_status(void)
{
    char buf[80];

    if (v7d998 != 0 && v7d998 != 3)
        return;
    if (v88ca0) {
        spr_x = 0;
        spr_y = 0x3C;
        spr_h = 0x4E;
        spr_w = 0x32;
        sprptr = img7eb5c;
        spr_draw();
        if (v88ca0 == 1) {
            sprptr = img7eb3c;
            spr_grab();
            v88ca0 = 2;
        }
    }
    if (v88bde) {
        spr_x = 4;
        spr_y = 1;
        strcpy(buf, pnames[v88c6a]);
        print_at(spr_x, spr_y, buf, 0);
        strcpy(buf, pnames[myplayer]);
        spr_x = 4;
        spr_y = 0x65;
        print_at(spr_x, spr_y, buf, 0);
    } else {
        if (!twoplayer) {
            spr_x = 4;
            spr_y = 0x32;
            strcpy(buf, pnames[v7eb80 ? (int)myplayer : (int)v88c6a]);
            print_at(spr_x, spr_y, buf, 0);
            spr_x = 0x11;
            spr_y = 6;
            spr_h = 0x26;
            spr_w = 0x29;
            sprptr = img7d83c;
            spr_draw();
            if (netvoices) {
                spr_x = 0x1A;
                spr_y = 0xE;
                spr_h = 0x13;
                spr_w = 0xE;
                sprptr = img7eb60;
                spr_draw();
            }
        }
        if (!splitscreen && !netvoices) {
            if (!twoplayer) {
                spr_x = 0x44;
                spr_y = 6;
                spr_h = 0x41;
                spr_w = 0x13;
                sprptr = img7d814;
                spr_draw();
            } else {
                spr_x = 4;
                spr_y = 0xB;
                if (netgame == 1)
                    strcpy(buf, pnames[myplayer]);
                else
                    strcpy(buf, pnames[v88c6a]);
                print_at(spr_x, spr_y, buf, 0);
                spr_x = 0x4A;
                print_at(spr_x, spr_y, "SCORE", 0);
                spr_x = 4;
                spr_y += 0x1E;
                if (netgame == 1)
                    strcpy(buf, pnames[v88c6a]);
                else
                    strcpy(buf, pnames[myplayer]);
                print_at(spr_x, spr_y, buf, 0);
                v7ec7c = spr_y;
                spr_x = 0x4A;
                print_at(spr_x, spr_y, "SCORE", 0);
            }
        }
        spr_x = 0x92;
        spr_y = 6;
        spr_h = 0x3D;
        spr_w = 0x13;
        sprptr = img7d810;
        spr_draw();
    }
    if (v7d998 == 3)
        print_at(0xE8, 6, "DEMO MODE ", 0);
    if (!twoplayer && !splitscreen && !netvoices && v88bb2 == 4)
        print_at(0xE8, 6, "TIME TRIAL", 0);
}

/* ---- one frame */

struct rec14 { char f00[0x14]; };
extern struct rec14 rec7c208[];
extern short v88b72, v88c8e, v88b4a, v88bf4, v88b78, v88c38, v7eb82;
extern volatile char key88ccd;
extern char *pal7d850, *p6a97c, *p7d714, *p7d718;
extern char arr79a28[];
extern char *p7d86c;
extern char *palptr;
void getpalette(void);
void page_copy(void);
void pcx_load(char *name, int n);
void fade_out(char *p);
void pcx_blit(void);                  /* asm 0x375cd: pushal..popal, no arguments, reads p7d714; every register survives */
void view_box(void);
void floor_draw(void);
void players_frames(void);
void players_project(void);
void ents_project(void);
void self_project(void);
void lights_project(void);
void shots_project(void);
void sprites_draw(void);
void floor_draw_upper(void);
void overlay_draw(void);
void hud_draw(void);
void anim_tick(void);
void page_show(void);
void fade_in(unsigned char *target);

void frame_draw(void)
{
    if (v7d998 == 2)
        pcx_load(rec7c208[v88c8e].f00, 1);
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        cars[v88b72].f96 = 0;
        cars[v88b72].f98 = 0;
        cars[v88b72].f94 = 0;
    }
    if (!v7d998) {
        palptr = pal7d850;
        getpalette();
        fade_out(pal7d850);
    }
    v88b4a = 0;
    page_draw = 16000;
    p7d714 = p6a97c;
    pcx_blit();
    page_draw = 16000;
    if (v7d998 != 0 && v7d998 != 3) {
        page_from = 16000;
        page_to = 32000;
        page_copy();
    }
    view_box();
    if (v7eb80)
        floor_draw();
    v88bf4 = 0;
    v88b78 = 0;
    p7d86c = arr79a28;
    players_frames();
    players_project();
    ents_project();
    self_project();
    if (v88c38)
        lights_project();
    shots_project();
    if (v88b78)
        sprites_draw();
    if (twoplayer || splitscreen) {
        view2_in();
        view_box();
        if (v7eb82) {
            if (v88bde)
                floor_draw_upper();
            else
                floor_draw();
        }
        v88bf4 = 0;
        v88b78 = 0;
        p7d86c = arr79a28;
        players_frames();
        players_project();
        ents_project();
        self_project();
        if (v88c38)
            lights_project();
        shots_project();
        if (v88b78)
            sprites_draw();
        view2_out();
    }
    if (v88ca0)
        v88ca0 = 1;
    draw_status();
    overlay_draw();
    hud_draw();
    anim_tick();
    draw_faces();
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (!v7d998)
        fade_in((unsigned char *)p7d718);
    page_draw = 0;
    page_from = 16000;
    page_to = 0;
    page_copy();
}

/* ---- the giggles / dope animations */

extern char *anidata;
extern int anidope, anitimer, anikick;
extern int aniphase, aniframe, ani7eb0c, ani7ea98, ani7ea8c, ani7ea94, anistart;
extern char *anip1, *anip2;
void ani_dope(void);

void ani_giggles(void)
{
    datfind("GIGGLES.SP");
    readdat(anidata, 0xB6E0);
    if (soundon) {
        datfind("QUACK.VOC");
        readdat(sfx2data, 0xE3A);
    }
    anidope = 0;
}

void ani_tick(void)
{
    if (anitimer >= 0x36) {
        anikick = 1;
        ani_dope();
        anitimer = 0;
    }
}

void ani_dope(void)
{
    if (!anidope) {
        datfind("DOPE.SP");
        readdat(anidata, 0x2E77);
        if (soundon) {
            datfind("BELCH.VOC");
            readdat(sfx2data, 0xD99);
        }
    }
    aniphase = 0;
    aniframe = 0;
    ani7eb0c = 0xC7;
    ani7ea98 = 0x78;
    ani7ea8c = 0;
    anip1 = anidata;
    ani7ea94 = 0xB0;
    anip2 = anidata + 0x2D00;
    anidope = 1;
    anistart = ticks2;
}

extern int ani7ea9c;
void drawsprite_clip(void);

void ani_draw(void)
{
    sprptr = anip1 + aniframe * (72 * 80);
    spr_x = ani7ea98;
    spr_w = 0x48;
    spr_y = ani7eb0c;
    spr_h = 0x50;
    if (spr_y <= 199)
        drawsprite_clip();
    if (ani7ea8c) {
        sprptr = anip2;
        spr_x = ani7ea94;
        spr_h = 0x19;
        spr_y = ani7ea9c;
        spr_w = 0xF;
        if (ani7ea8c == 1) {
            ani7ea8c = 2;
            spr_x++;
        } else {
            ani7ea8c = 1;
            spr_x--;
        }
        if (spr_y <= 199)
            drawsprite_clip();
        if (ani7ea9c == 0x6E) {
            ani7ea8c = 0;
        } else {
            ani7ea9c -= 2;
            if (ani7ea9c < 0x6E)
                ani7ea9c = 0x6E;
        }
    }
    switch (aniphase) {
    case 0:
        ani7eb0c -= 8;
        if (ani7eb0c <= 0x70) {
            ani7eb0c = 0x70;
            aniphase = 1;
            anistart = ticks2;
        }
        break;
    case 1:
        if (since2(anistart) >= 0x10) {
            aniphase = 2;
            anistart = ticks2;
            aniframe = 1;
            ani7ea8c = 1;
            ani7ea9c = ani7eb0c + 0x25;
            sfx_play2();
        }
        break;
    case 2:
        if (since2(anistart) >= 0x14) {
            aniphase = 3;
            aniframe = 0;
        }
        break;
    case 3:
        ani7eb0c += 4;
        if (ani7eb0c > 0xC7)
            anikick = 0;
        break;
    }
}

/* ---- sprites */

extern char *wfont, *ajdata, *pufdata, *handledata, *lapdata, *hogdata, *sparkdata, *ofont, *ofont2, *ofont3, *genef, *crackdata, *icons;
extern char *sp6a518[], *sp6a538[], *sp6a548[], *sp6a778[], *sp6a878[], *sp6a888[], *sp7b210[], *sp7c0b0[], *sp7c1d8[], *sp6e410[], *sp6e440[], *sp7c800[], *sp7d6d8[];
extern char *ic7ea84, *hm7d740, *hm7d73c, *hm7d738, *hm7d734, *hm7d730, *hm7d72c, *hm7d724, *hm7d720, *hm7d71c, *hm7d728, *lp6e720, *lp6e724, *lp7d804, *gn7eb24, *gn7eb28, *gn7eb2c, *gn7eb40, *gn7eb44, *gn7eb48, *gn7c7f0, *gn7c7f4, *gn7c7f8, *gn7c7fc, *gn7eae8, *gn7c528, *gn7c52c, *gn7eae4;
extern short nsprattr;
extern char sprattr[];

void sprites_load(void)
{
    char *p;
    int i;

    ani_giggles();
    datfind("WFONT1.SP");
    readdat(wfont, 0x1FFE);
    datfind("AJ.SP");
    readdat(ajdata, 0x10A0);
    datfind("PUF.SP");
    readdat(pufdata, 0x10A0);
    datfind("HANDLE.SP");
    readdat(handledata, 0xE60);
    datfind("LAP.SP");
    readdat(lapdata, 0x4C56);
    datfind("HOGMIS.SP");
    readdat(hogdata, 0x17C4);
    datfind("SPARK.SP");
    readdat(sparkdata, 0x168);
    datfind("OFONT.SP");
    readdat(ofont, 0xF40);
    readdat(ofont2, 0xEBC);
    readdat(ofont3, 0x2240);
    datfind("GENEF.SP");
    readdat(genef, 0x3964);
    datfind("CRACK.SP");
    readdat(crackdata, 0x1458);
    datfind("ICONS.SP");
    readdat(icons, 0x15C6);
    p = ofont3;
    for (i = 0; i < 137; i++) {
        sp6a548[i] = p;
        p += 0x40;
    }
    p = anidata;
    for (i = 0; i < 7; i++) {
        sp7b210[i] = p;
        p += 0x1A20;
    }
    p = icons;
    for (i = 0; i < 6; i++) {
        sp7c0b0[i] = p;
        p += 0x222;
    }
    ic7ea84 = p;
    p += 0x31E;
    for (i = 0; i < 10; i++) {
        sp7c1d8[i] = p;
        p += 0x96;
    }
    p = hogdata;
    p += 0x2BE;
    hm7d740 = p;
    p += 0x2BE;
    hm7d73c = p;
    p += 0xEA;
    hm7d738 = p;
    p += 0xEA;
    hm7d734 = p;
    p += 0xEA;
    hm7d730 = p;
    p += 0xEA;
    hm7d72c = p;
    p += 0xEA;
    hm7d724 = p;
    p += 0x3A8;
    hm7d720 = p;
    p += 0x3A8;
    hm7d71c = p;
    p += 0x3A8;
    hm7d728 = p;
    p = lapdata;
    p += 0x13F0;
    for (i = 0; i < 10; i++) {
        sp6e410[i] = p;
        p += 0x27E;
    }
    lp6e720 = p;
    p += 0x550;
    lp6e724 = p;
    p += 0x550;
    img7d83c = p;
    p += 0x616;
    img7d814 = p;
    p += 0x4D3;
    img7d810 = p;
    p += 0x487;
    for (i = 0; i < 10; i++) {
        sp6e440[i] = p;
        p += 0x5A;
    }
    lp7d804 = p;
    datfind("SPRITE.ATR");
    readdat(&nsprattr, 2);
    readdat(sprattr, nsprattr * 40);
    p = wfont;
    for (i = 0; i < 42; i++) {
        sp6a888[i] = p;
        p += 0xC3;
    }
    p = ajdata;
    for (i = 0; i < 4; i++) {
        sp6a878[i] = p;
        p += 0x428;
    }
    p = pufdata;
    for (i = 0; i < 4; i++) {
        sp6a538[i] = p;
        p += 0x428;
    }
    p = handledata;
    for (i = 0; i < 8; i++) {
        sp6a518[i] = p;
        p += 0x1CC;
    }
    p = ofont;
    for (i = 0; i < 61; i++) {
        sp6a778[i] = p;
        p += 0x40;
    }
    p = genef;
    for (i = 0; i < 6; i++) {
        sp7c800[i] = p;
        p += 0x284;
    }
    gn7eb24 = p;
    p += 0xC3;
    gn7eb28 = p;
    p += 0xC3;
    gn7eb2c = p;
    p += 0xC3;
    gn7eb40 = p;
    p += 0x428;
    gn7eb44 = p;
    p += 0x428;
    gn7eb48 = p;
    p += 0x428;
    gn7c7f0 = p;
    p += 0x428;
    gn7c7f4 = p;
    p += 0x428;
    gn7c7f8 = p;
    p += 0x428;
    gn7c7fc = p;
    p += 0x428;
    gn7eae8 = p;
    p += 0x428;
    gn7c528 = p;
    p += 0x134;
    gn7c52c = p;
    p += 0x134;
    gn7eae4 = p;
    p += 0x9C;
    for (i = 0; i < 11; i++) {
        sp7d6d8[i] = p;
        p += 0x3F;
    }
    img7eb60 = p;
}

/* ---- the data file (readdat, datsize and seekdat are inlined into callers before and after them) */

int datopen(char *name)
{
    short n;

    datfile = fopen(name, "rb");
    if (datfile == NULL)
        return -1;
    datidx = 0;
    readdat(&n, 2);
    readdat(datdir, n * 22);
    return 1;
}

short datfind(char *name)
{
    short found;

    strupr(name);
    datidx = 0;
    for (;;) {
        if (datdir[datidx].name[0] == 0) {
            found = 0;
            break;
        }
        if (strcmp(name, datdir[datidx].name) == 0) {
            found = 1;
            break;
        }
        datidx++;
    }
    if (found)
        seekdat(datdir[datidx].offset + 2);
    return found;
}

void datclose(void)
{
    if (datfile)
        fclose(datfile);
}

void seekdat(long ofs)
{
    fseek(datfile, ofs, SEEK_SET);
}

long telldat(void)
{
    return ftell(datfile);
}

long datsize(void)
{
    return datdir[datidx].size;
}

void readdat(void *buf, int n)
{
    fread(buf, n, 1, datfile);
}

/* The game CALLS readdat (24 sites), seekdat, telldat and datsize (11-12 each) everywhere; defined
 * above, -oe would inline their bodies. Later sites call them under another name (same symbol), which
 * -oe cannot inline. */
void readdat_x(void *buf, int n);
#pragma aux readdat_x "readdat_"
#define readdat readdat_x
void seekdat_x(long ofs);
long telldat_x(void);
long datsize_x(void);
#pragma aux seekdat_x "seekdat_"
#pragma aux telldat_x "telldat_"
#pragma aux datsize_x "datsize_"
#define seekdat seekdat_x
#define telldat telldat_x
#define datsize datsize_x

/* ---- centered double-print (0x17888) */

void fn_17888(char *s, short y)
{
    int save;
    int x;

    save = page_draw;
    page_draw = 0;
    x = (0x140 - (short)((int)strlen(s) * 8)) / 2;
    if (y == 0)
        y = 0x60;
    print_at((short)x, y, s, 0);
    page_draw = 0x3e80;
    print_at((short)x, y, s, 0);
    page_draw = save;
}

/* ---- my DAT player record load (0x173C4) */

/* differs (100 rows, 1152 vs the game's 1183 bytes; iter 45 189 -> 100): use `myplayer` itself,
   not a `short mp` local - 9.5b then re-reads it after each call (mov di,[0x88b96] 0x17405,
   0x17435, 0x17483) and recomputes `myplayer * 0x98` at the top of every loop like the game; a
   local CSEs all of it away (nmrefs showed 3 extra `faces` refs, and 6 jump blocks only one side).
   Same for the record-pointer local `rec`: written as `faces[myplayer]` inside the loops. The
   v7d87c fan is `*q = ...; q += 2` (the game's five `add ebp,8` 0x1750f-0x1753f; the index form
   q[0],q[2],... scores 115, `*q++` scores 110 but steps 4 bytes - wrong, rejected). The two
   `while (i <= v88c4e/v88c52)` loops became `for (i = 1, eax = 4; ...; i++)`: 9.5b then puts the
   test at the bottom with the global re-read (0x175d9-0x175e2), where a `while` keeps a top test.
   Left: q homes ECX, the game EBP (14 rows); in the img[] fan blocks `myplayer * 0x98` and
   `v88b7e` hold EAX/EDX the other way round (24 rows); the sp7b374b loop keeps its descending
   cursor in EAX and 0x10 in EDX (the game ESI/EDX, 0x175ed-0x17623).
   behaviour pass (iter 59), 3 BUGs, all of one class - a data address of ours that is wrong, which
   fndiff and nmrefs cannot see (nmrefs labels an indexed access by the array it lands in, and our
   disp32 data operands are linker fixups): the eight-write fan ending the function used `sp7b230b`,
   which links to 0x7b230 (.work/link/symmap.txt:387), where the game writes 0x7b238..0x7b270: 0x177ac
   `mov [eax*4+0x7b238],ebx` (bytes 89 1c 95 38 b2 07 00) through 0x17810 `mov [eax*4+0x7b270],edx`,
   in the same img[8],9,8,4,10,11,10,4 order we use. No instruction in the image mentions 0x7b230
   (tools/le/xref.py 0x7b230 finds nothing), while 0x7b238 is the base stored into curkart+0x4e at
   0x3174a and walked by 0x2bff0 (xref 0x7b238) - every pair was one slot low and 0x7b270 was never
   written. Now `lit7b238` = 0x7b238 (.work/link/symmap.txt:1024), the same symbol 0x3174a's copy
   uses. The two record fans read the face record 8 bytes high: the sp7b324b loop read
   faces+eax+0x44 where the game reads faces+eax+0x3c (`mov edx,[ebx+eax+0x73c44]` 0x17594, faces =
   0x73c08) and the sp7b2d0b loop +0x60 where the game reads +0x58 (`mov ebx,[ebx+eax+0x73c60]`
   0x175cb) - eight dwords each, so every entry was the wrong field. fndiff rows (100) and the MATCH
   count (202) did not move for any of the three: that disp32 is a linker fixup, and libmatch reports
   its addend as 0 while the addend (0,8,..,0x38) lives in the .OBJ bytes at the fixup location
   (GOTCHAS). tools/wc/globrefs.py (new this iter) found all three: it resolves our ext fixups to
   linear addresses (symmap address + the addend in the .OBJ bytes) and prints the data addresses only
   one side touches plus equal-address count gaps; after the fixes its only line here is
   `counts 0x7d868 ours x1 game x2`, which is the SAME below.
   SAME: the `game more 1x v7d868` is the game's dead entry load 0x173d1 (EBP is untouched until
   `mov ebp,[0x7d87c]` 0x17500); our 9.5b folds the `q = (int *)(size_t)v7d868` assignment away, and
   the write-back 0x1784f `mov [0x7d868],ebp` (EBP = v7d87c+0x28) is our last statement
   `v7d868 = (int)q`. The `consts ours only #0x28` is our one `add 0x28` for the game's five
   `add ebp,8` 0x17551-0x1755d; `+0x4 +0xc +0x14 +0x1c +0x24 +0x2c` are our `faces` field offsets
   (img[1],img[3],img[5],img[7],img[9],img[11] reads), the game reaching the same six addresses with
   folded absolute operands (`mov edx,[eax+0x73c0c]` 0x17506 .. `[eax+0x73c24]` 0x17542) plus
   `[eax+0x74190]`/`[esi+0x73c38]` 0x1782f-0x17835 in the arr7418c block, which nmrefs masks as
   absolute. `#0x2` is our `add esi,2`
   cursor in the sp7b374b loop against the game's `mov ebx,1` 0x17625 - the loop shape above. */

extern short v88b7e;
extern short v88c4e, v88c50, v88c52;
extern int v7d6c8, v7d868, v7d87c, v7d884, v7d888;
extern char arr7bba0[];               /* per-player 12-byte DAT file names */
extern char sp7b2d0b[], sp7b324b[], sp7b374b[];
extern char sp7b418b[], sp7b448b[], sp7b4d8b[], sp7b518b[];
extern char arr7418c[];
void fn_173c4(void);

void fn_173c4(void)
{
    int *q;
    int eax;                       /* byte cursor (name kept: matches game shape) */
    int i;
    int esi, edx;
    char *p;

    q = (int *)(size_t)v7d868;
    v7d884 = (int)(size_t)lit70660;
    v7d888 = (int)(size_t)lit6e728;
    if (v88b7e == 0) {
        datfind(arr7bba0 + myplayer * 0xc);
        fread((void *)(size_t)v7d6c8, faces[myplayer].f94, 1, datfile);
        v7d87c = v7d884;
    } else {
        v7d87c = (int)(size_t)lit6e728;
        p = (char *)(size_t)v7d6c8 + 0x10a0;
        datfind(arr7bba0 + myplayer * 0xc);
        fread(p, 0x10a0, 1, datfile);
        eax = 0;
        do {
            eax += 4;
            *(int *)(arr7418c + eax) = *(int *)((char *)&faces[myplayer] + eax + 0x2c);
            *(int *)(arr7418c + eax + 8) = *(int *)((char *)&faces[myplayer] + eax + 0x34);
        } while (eax != 8);
        eax = 0;
        do {
            eax += 4;
            *(int *)((char *)&faces[myplayer] + eax + 0x2c) = (int)p;
            p += 0x428;
        } while (eax != 8);
        eax = 0;
        do {
            eax += 4;
            *(int *)((char *)&faces[myplayer] + eax + 0x34) = (int)p;
            p += 0x428;
        } while (eax != 8);
    }
    q = (int *)(size_t)v7d87c;
    *q = (int)faces[myplayer].img[1]; q += 2;
    *q = (int)faces[myplayer].img[2]; q += 2;
    *q = (int)faces[myplayer].img[3]; q += 2;
    *q = (int)faces[myplayer].img[5]; q += 2;
    *q = (int)faces[myplayer].img[6]; q += 2;
    *q = faces[myplayer].img[7];
    q = (int *)(size_t)v7d87c;
    q += 10;
    *(short *)((char *)q - 0x24) = 0x11;
    *(short *)((char *)q - 0x1c) = 0xa;
    *(short *)((char *)q - 0x14) = 0;
    *(short *)((char *)q - 0xc) = 0x12;
    *(short *)((char *)q - 4) = 8;
    *(short *)((char *)q + 4) = 1;
    eax = 0;
    do {
        eax += 4;
        /* faces + eax + 0x3c: the game's `mov edx,[ebx+eax+0x73c44]` 0x17594 (faces = 0x73c08) */
        *(int *)(sp7b324b + eax) = *(int *)((char *)&faces[myplayer] + eax + 0x3c);
    } while (eax != 0x20);
    v88c4e = faces[myplayer].f90;
    for (i = 1, eax = 4; i <= v88c4e; i++) {
        eax += 4;
        /* faces + eax + 0x58: the game's `mov ebx,[ebx+eax+0x73c60]` 0x175cb */
        *(int *)(sp7b2d0b + eax) = *(int *)((char *)&faces[myplayer] + eax + 0x58);
    }
    v88c52 = faces[myplayer].f92;
    esi = 0x10;
    edx = 0;
    do {
        edx += 4;
        *(int *)(sp7b374b + edx) = *(int *)((char *)&faces[myplayer] + esi);
        esi -= 4;
    } while (esi != -4);
    for (i = 1, esi = 4, edx = 0; i <= v88c52; i++) {
        esi += 4;
        edx += 4;
        *(int *)(sp7b374b + edx) = *(int *)((char *)&faces[myplayer] + esi + 0x68);
        i++;
    }
    *(int *)(sp7b518b + v88b7e * 4) = faces[myplayer].img[12];   /* +0x30 */
    *(int *)(sp7b518b + 8 + v88b7e * 4) = faces[myplayer].img[13];
    *(int *)(sp7b518b + 0x10 + v88b7e * 4) = faces[myplayer].img[4];
    *(int *)(sp7b518b + 0x18 + v88b7e * 4) = faces[myplayer].img[14];
    *(int *)(sp7b518b + 0x20 + v88b7e * 4) = faces[myplayer].img[15];
    *(int *)(sp7b4d8b + v88b7e * 4) = faces[myplayer].img[3];
    *(int *)(sp7b4d8b + 8 + v88b7e * 4) = faces[myplayer].img[12];
    *(int *)(sp7b4d8b + 0x10 + v88b7e * 4) = faces[myplayer].img[13];
    *(int *)(sp7b4d8b + 0x18 + v88b7e * 4) = faces[myplayer].img[4];
    *(int *)(sp7b4d8b + 0x20 + v88b7e * 4) = faces[myplayer].img[14];
    *(int *)(sp7b4d8b + 0x28 + v88b7e * 4) = faces[myplayer].img[15];
    *(int *)(sp7b4d8b + 0x30 + v88b7e * 4) = faces[myplayer].img[5];   /* 0x1770e: 0x7b508, the 7th of sp7b4d8b */
    v88c52 += 5;
    eax = 4;
    edx = 0;
    do {
        *(int *)(sp7b448b + edx + v88b7e * 4) = (int)faces[myplayer].img[eax];
        if (++eax > 7)
            eax = 0;
        edx += 8;
    } while (edx != 0x88);
    *(int *)(sp7b418b + v88b7e * 4) = faces[myplayer].img[9];    /* +0x24 */
    *(int *)(sp7b418b + 8 + v88b7e * 4) = faces[myplayer].img[8];
    *(int *)(sp7b418b + 0x10 + v88b7e * 4) = faces[myplayer].img[4];
    *(int *)(sp7b418b + 0x18 + v88b7e * 4) = faces[myplayer].img[10];
    *(int *)(sp7b418b + 0x20 + v88b7e * 4) = faces[myplayer].img[11];
    /* the 0x7b238 animation table, NOT 0x7b230: the game's writes carry the absolute base
       0x7b238..0x7b270 (0x177ac `mov [eax*4+0x7b238],ebx` .. 0x17810 `mov [eax*4+0x7b270],edx`,
       bytes 38 b2 07 00 at 0x177ac) and no code in the image mentions 0x7b230; 0x7b238 is the
       base stored into curkart+0x4e at 0x3174a and walked by 0x2bff0 (xref 0x7b238). */
    *(int *)(lit7b238 + v88b7e * 4) = faces[myplayer].img[8];
    *(int *)(lit7b238 + 8 + v88b7e * 4) = faces[myplayer].img[9];
    *(int *)(lit7b238 + 0x10 + v88b7e * 4) = faces[myplayer].img[8];
    *(int *)(lit7b238 + 0x18 + v88b7e * 4) = faces[myplayer].img[4];
    *(int *)(lit7b238 + 0x20 + v88b7e * 4) = faces[myplayer].img[10];
    *(int *)(lit7b238 + 0x28 + v88b7e * 4) = faces[myplayer].img[11];
    *(int *)(lit7b238 + 0x30 + v88b7e * 4) = faces[myplayer].img[10];
    *(int *)(lit7b238 + 0x38 + v88b7e * 4) = faces[myplayer].img[4];
    v88c50 = 8;
    if (v88b7e != 0) {
        eax = 0;
        do {
            *(int *)((char *)&faces[myplayer] + 0x30 + eax) = *(int *)(arr7418c + eax + 4);
            *(int *)((char *)&faces[myplayer] + 0x38 + eax) = *(int *)(arr7418c + eax + 0xc);
            eax += 4;
        } while (eax != 8);
    }
    v7d868 = (int)q;
}

/* ---- the menu pointers (0x73938) */

struct ent {
    short id;
    short f2;
    short x, y;
    short f8;
    short frame;
};

extern struct ent ents[9];      /* 0x73938; the 9th slot is the car-select box (cars_menu) */
extern short nents;

void ents_init(void)
{
    short i;

    nents = 4;
    for (i = 0; i < 4; i++) {
        ents[i].f2 = 0;
        ents[i].id = i;
        ents[i].f8 = 0;
    }
    ents[0].frame = 0;
    ents[0].x = 0x1B;
    ents[0].y = 0x10;
    ents[1].frame = 0;
    ents[1].x = 0x1B;
    ents[1].y = 0xB8;
    ents[2].frame = 7;
    ents[2].x = 0x126;
    ents[2].y = 0x10;
    ents[3].frame = 7;
    ents[3].x = 0x126;
    ents[3].y = 0xB8;
    for (i = 0; i <= 3; i++) {
        ents[i].x -= 0x13;
        ents[i].y -= 0xE;
    }
}

void ents_draw(int n)
{
    int i;

    for (i = 0; i < n; i++) {
        sprptr = sp6a538[ents[i].frame];
        spr_x = ents[i].x;
        spr_y = ents[i].y;
        spr_h = 0x26;
        spr_w = 0x1C;
        spr_draw();
    }
}

struct attr {                   /* SPRITE.ATR, 36 records of 40 bytes: layout and evidence in
                                 * findings/formats.md "SPRITE.ATR"; the game reads only these offsets */
    short kind;                 /* 0 the 12 OB sets, 1 OB1, 2 the 18 H sets, 3..7 one set each
                                 * (3 = id 0, in every .SPW); see ents_flags / ent_sprites_load */
    short f02;                  /* 10 in all 36 records, never read */
    short w, h;                 /* payload w * h * nframes == the DAT entry's size (census of 36) */
    char f08[0x0C - 8];         /* a copy of w,h in all 36 records, never read */
    short f0c;                  /* size class: tab6e470[f0c * 20 + step] (ents_project 0x25c73);
                                 * 3 = 32x24, 2 = 14x28, 1 = 28x28 */
    short nframes;
    char f10[8];                /* the WACKY.DAT entry to load: H1..H25.SP (no H3/H13) then OB1..OB13.SP */
    char f18[0x24 - 0x18];      /* zero in the file, never read */
    char *data;                 /* filled by ent_sprites_load 0x2136f */
};

extern short *p7d838, *p7d764;
extern int v7d878;
extern short *tab6e470[], *tab6e560[];
extern int v7d338[];
extern short v88c68;
void spr_draw_scaled(void);

/* differs (36 rows, 29 of them the masked-fixup artefact [reg - @]/[reg + @]): a three-way stack-slot
   permutation of n, animate and the w*h temporary (game n [esp+4], animate [esp+8], temp [esp]; ours
   n [esp+8], animate [esp], temp [esp+4]) - the slot dead end, plus the 4-byte size gap it causes
   ([esp] has no displacement byte). The braced `if (..) { two = 1; } else { two = 0; }` (iter 37)
   gave the game's `mov ebx,1`/`xor ebx,ebx` AND its loop shape (jmp to the bottom test, one
   epilogue): 49 -> 36. An explicit int wh for the temp (3 declaration positions x 2 forms) moves
   nothing. Behaviour pass (iter 63), 1 item: the one-sided jump-target block is SAME-shape - the
   game's `jmp 0x146bd` 0x145c3 (right after `call spr_draw_scaled` 0x145be, the end of the `if (sel != -1)`
   arm) leaves the loop body at the loop tail 0x146bd `inc edx; cmp eax,[esp+4]; jl 0x14398`, because
   sel != -1 is known on that path and the two blocks it skips are both `sel == -1 && ...`
   (0x14623-0x1462b and 0x1468d-0x14695, the second testing sel before EBX, so the stale EBX is never
   read there); our build jumps to the twoplayer test 0x14603 and re-reads sel at 0x14620/0x1468a.
   Same reachable paths and values - refs 76/76 same, no one-sided const, nothing else open. */
void ents_show(short sel, int n, int animate, int custom, int skip)
{
    short i;
    short id;
    int two;

    for (i = 0; i < n; i++) {
        if (skip && (i == 1 || i == 3))
            continue;
        if (!custom) {
            sprptr = faces[ents[i].id].img[ents[i].frame];
        } else {
            id = ents[i].id;
            sprptr = ((struct attr *)sprattr)[id].data
                + ents[i].frame * (((struct attr *)sprattr)[id].w * ((struct attr *)sprattr)[id].h);
        }
        if (animate) {
            if (++ents[i].f8 > 1) {
                if (!custom) {
                    if (i < 2 || sel == -1) {
                        if (++ents[i].frame > 7)
                            ents[i].frame = 0;
                    } else {
                        if (--ents[i].frame < 0)
                            ents[i].frame = 7;
                    }
                } else {
                    if (++ents[i].frame == ((struct attr *)sprattr)[id].nframes)
                        ents[i].frame = 0;
                }
                ents[i].f8 = 0;
            }
        }
        spr_x = ents[i].x;
        spr_y = ents[i].y;
        if (sel != -1) {
            if (!custom)
                p7d838 = tab6e470[sel];
            else
                p7d838 = tab6e560[sel];
            p7d764 = p7d838;
            v7d878 = 0;
            p7d838 += 2;
            spr_h = *p7d764++;
            spr_w = *p7d764;
            if (!custom) {
                spr_x += 0x13;
                spr_y += 0xE;
            } else {
                spr_x += ((struct attr *)sprattr)[id].w >> 1;
                spr_y += ((struct attr *)sprattr)[id].h >> 1;
            }
            spr_y -= spr_w >> 1;
            spr_x -= spr_h >> 1;
            spr_draw_scaled();
        } else {
            if (!custom) {
                spr_h = 0x26;
                spr_w = 0x1C;
            } else {
                spr_h = ((struct attr *)sprattr)[id].w;
                spr_w = ((struct attr *)sprattr)[id].h;
            }
            spr_draw();
        }
        if (twoplayer || splitscreen) {
            two = 1;
        } else {
            two = 0;
        }
        if (sel == -1 && v7d338[i]) {
            spr_h = 0x36;
            spr_w = 9;
            sprptr = lp7d804;
            spr_x -= 0xB;
            spr_y += 0x12;
            spr_draw();
            spr_h = 0x26;
            spr_w = 0x1C;
            spr_x += 0xB;
            spr_y -= 0x12;
        }
        if (sel == -1 && two && v88b7e == 1 && i == v88c68) {
            sprptr = gn7eae8;
            spr_draw();
        }
    }
}

/* ---- text */

extern short v88c2e, v88c10;

void wtext(int x, int y, char *s, int scaled)
{
    int i, ok, c, xv;
    volatile int sl[4];       /* volatile: keep it a real array, the game's slots 0/4/8/0xc */
    short sx, sy;

    sl[0] = x;
    sl[1] = scaled;
    i = 0;
    if (strlen(s) == 0)
        return;
    if (sl[1]) {
        spr_h = v88c2e;
        spr_w = v88c10;
        sl[2] = spr_h >> 1;
        sl[3] = spr_w >> 1;
    } else {
        spr_h = 0xF;
        spr_w = 0xD;
    }
    xv = sl[0];
    spr_y = y;
    spr_x = xv;
    for (; s[i] != 0; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            c = s[i] - 'A';
            ok = 1;
        } else if (s[i] >= '0' && s[i] <= '9') {
            c = s[i] - '0' + 26;
            ok = 1;
        } else {
            ok = 1;
            switch (s[i]) {
            case '!':
                c = 0x28;
                break;
            case '?':
            case '~':
                c = 0x29;
                break;
            case ' ':
                c = 0x24;
                break;
            case '.':
                c = 0x25;
                break;
            case '<':
                c = 0x26;
                break;
            case '>':
            case '#':
                c = 0x27;
                break;
            default:
                ok = 0;
            }
        }
        if (!ok)
            c = 0x24;
        sprptr = sp6a888[c];
        if (sl[1]) {
            sx = spr_x;
            sy = spr_y;
            spr_x += 7;
            spr_y += 6;
            spr_x -= sl[2];
            spr_y -= sl[3];
            spr_draw_scaled();
            spr_x = sx;
            spr_y = sy;
        } else {
            spr_draw();
        }
        spr_x += 0xB;
    }
}

extern char *sfont[];
extern char border, ink;
void spr_draw_opaque(void);
void box_draw(void);

void stext(int x, int y, char *s)
{
    int i, n;

    spr_h = 8;
    spr_w = 8;
    spr_x = x;
    spr_y = y;
    n = strlen(s);
    for (i = 1; i <= n; i++) {
        sprptr = sfont[(unsigned char)s[i - 1]];
        spr_draw_opaque();
        spr_x += 8;
    }
}

void print_at(short x, int y, char *s, short mode)
{
    int i, ok, c;

    i = 0;
    if (strlen(s) == 0)
        return;
    if (mode != 0) {
        if (mode != 2) {
            spr_x = x - 4;
            spr_y = y - 4;
            spr_h = strlen(s) * 8 + 8;
            spr_w = 0x10;
            ink = 0x2C;
            border = 0x26;
        } else {
            spr_x = x;
            border = 0x2C;
            ink = 0x2C;
            spr_y = y;
            spr_h = strlen(s) * 8;
            spr_w = 8;
        }
        box_draw();
    }
    spr_x = x;
    spr_h = 8;
    spr_w = 8;
    spr_y = y;
    for (; s[i] != 0; i++) {
        ok = 0;
        if (s[i] >= 'A' && s[i] <= 'Z') {
            c = s[i] - 'A';
            ok = 1;
        } else if (s[i] >= '0' && s[i] <= '9') {
            c = s[i] - '0' + 26;
            ok = 1;
        } else {
            switch (s[i]) {
            case '!':
                c = 0x24;
                ok = 1;
                break;
            case '.':
            case ':':
                c = 0x35;
                ok = 1;
                break;
            case '#':
                c = 0x26;
                ok = 1;
                break;
            case ',':
                c = 0x36;
                ok = 1;
                break;
            case '-':
                c = 0x37;
                ok = 1;
                break;
            case '\'':
                c = 0x39;
                ok = 1;
                break;
            case '@':
                c = 0x38;
                ok = 1;
                break;
            case '(':
                c = 0x2C;
                ok = 1;
                break;
            case ')':
                c = 0x2D;
                ok = 1;
                break;
            case '?':
                c = 0x34;
                ok = 1;
                break;
            case '&':
                c = 0x2A;
                ok = 1;
                break;
            }
        }
        if (ok) {
            sprptr = sp6a778[c];
            spr_draw();
        }
        spr_x += 8;
    }
}

/* ---- controls, timing */

extern short joy_cx, joy_cy, joy_lim_l, joy_lim_r, joy_lim_u, joy_lim_d;

void ctl_setup(short which)
{
    joy_skip = 0;
    if (joyfound)
        joy_skip = 1;
    if (which == 0) {
        if (!v7ee4a)
            joy_skip = 1;
        joy_cx = ((short *)cfg7ee1c)[14];
        joy_cy = ((short *)cfg7ee1c)[15];
        joy_lim_l = ((short *)cfg7ee1c)[16];
        joy_lim_r = ((short *)cfg7ee1c)[17];
        joy_lim_u = ((short *)cfg7ee1c)[18];
        joy_lim_d = ((short *)cfg7ee1c)[19];
        joy_xbit = 1;
        joy_ybit = 2;
        joy_bits = 3;
    } else {
        if (((short *)cfg77128)[2])
            joy_skip = 1;
        joy_cx = ((short *)cfg77128)[4];
        joy_cy = ((short *)cfg77128)[5];
        joy_lim_l = ((short *)cfg77128)[6];
        joy_lim_r = ((short *)cfg77128)[7];
        joy_lim_u = ((short *)cfg77128)[8];
        joy_lim_d = ((short *)cfg77128)[9];
        joy_xbit = 4;
        joy_ybit = 8;
        joy_bits = 12;
    }
    joy_poll();
}

int center_x(int w)
{
    return (320 - w) / 2;
}

int center_y(int h)
{
    return (200 - h) / 2;
}

extern volatile int ticks3;
extern int v7edbc;

void wait_ticks3(void)
{
    ticks3 = 0;
    while (ticks3 < v7edbc)
        ;
}

void delay(int n)
{
    int start;

    start = ticks;
    while (ticks - start < n)
        ;
}

extern volatile char key88cd0;
extern int v7ebc0, v7ebb0;

void wait_or_key(int n)
{
    ticks = 0;
    ticks2 = 0;
    do {
        if (key88cd0 == 1 && v7ebc0) {
            v7ebb0 = 1;
            break;
        }
    } while (n > ticks);
}

void joy_reset(void)
{
    joyfound = 0;
    joy_skip = 0;
    joy_poll();
}

extern char v7b6dc, v7b6dd;

void joy_clear_y(void)
{
    if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
        joy88b40 = 0;
}

/* ---- chat mode */

extern int v7ed34, v7ed20, v7ed1c;
extern int oldkbd_off;
extern short oldkbd_seg;
extern unsigned char v88cf3, v88cf0, v88cef;
void kbd_off(void);
void kbd_on(void);
short net_key(unsigned char key, unsigned char flag, int x);

/* differs: stack slots only (QUESTIONS 2): the save slots permute (sx 0x1b8, sw 0x1bc, s34 0x1c4 ... vs our
   0x1b8 s34 ...) and the five 80-byte buffers too: game mine@0, blank@0x50, theirs@0xa0, buf@0xf0,
   ch@0x140; ours still has them in another order for declaration order
   blank,mine,theirs,buf,ch (not plain reverse). Statement order around strlen/center_x also scheduled
   differently (spr_y=10 store after strlen in the game); ours is also 16 bytes longer. Eight
   declaration orders probed in one compile (.work/probe_sp/SP6.C): the best is 2 rows below its control
   (182 vs 184) and none lands the game's buffer layout - 9.5b places arrays by decl + first use
   (fn_1832c's comment), and no order here gets it. Kept: declaration order blank,mine,theirs,buf,ch,
   196 -> 194. */
void chat(void)
{
    char blank[80], mine[80], theirs[80], buf[80], ch[80];
    union REGS r;
    struct SREGS s;
    int n1, t;
    int x1, key, sy, y1m, n2, y1, y2m, c9, sh, y2, x2, s1c, sw, s20, sx, other, c8, s34;

    save_a = page_draw;
    save_c = page_to;
    save_b = page_from;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    s34 = v7ed34;
    s20 = v7ed20;
    s1c = v7ed1c;
    sh = spr_h;
    spr_h = 320;
    sw = spr_w;
    spr_w = 200;
    c9 = ink;
    ink = 0x2C;
    c8 = border;
    border = 0x26;
    sx = spr_x;
    spr_x = 0;
    sy = spr_y;
    spr_y = 0;
    box_draw();
    while (key88cd0 == 0x2E)
        ;
    r.x.eax = 0x2509;
    s.es = 0;
    r.x.edx = oldkbd_off;
    s.ds = oldkbd_seg;
    int386x(0x21, &r, &r, &s);
    kbd_off();
    strcpy(buf, "PRESS ESC TO END CHAT MODE");
    spr_y = 10;
    spr_x = center_x(strlen(buf) * 8);
    print_at(spr_x, spr_y, buf, 0);
    strcpy(buf, "BI-DIRECTIONAL CHITTER CHATTER!");
    spr_y = 0x14;
    spr_x = center_x(strlen(buf) * 8);
    print_at(spr_x, spr_y, buf, 0);
    strcpy(buf, " YOUR GOOD SELF ");
    spr_y = 0x28;
    spr_x = center_x(strlen(buf) * 11);
    wtext(spr_x, spr_y, buf, 0);
    strcpy(buf, " THE OTHER DUDE ");
    spr_y = 0x71;
    spr_x = center_x(strlen(buf) * 11);
    wtext(spr_x, spr_y, buf, 0);
    strcpy(mine, "");
    strcpy(theirs, "");
    while (strlen(mine) < 38)
        strcat(mine, " ");
    while (strlen(theirs) < 38)
        strcat(theirs, " ");
    y1 = 0x49;
    x2 = 8;
    x1 = 8;
    y2 = 0x8F;
    strcpy(blank, mine);
    n1 = 0;
    v7ed20 = 0x1C;
    v7ed1c = 0x220;
    n2 = 0;
    delay(0x18);
    y2m = y2 - 10;
    y1m = y1 - 10;
    net88c14 = 0;
    for (;;) {
        t = ticks;
        key = 0;
        v88cf3 = 0;
        if (kbhit())
            key = getch();
        v88cf0 = key;
        net88c14 = net_key(v88cf0, v88cf3, 0);
        other = v88cef;
        if (key == 27 || other == 27 || net88c14)
            break;
        if (key == 13) {
            stext((short)x1, (short)y1m, mine);
            strcpy(mine, blank);
            stext((short)x1, (short)y1, mine);
            n1 = 0;
        }
        if (13 == other) {
            stext((short)x2, (short)y2m, theirs);
            strcpy(theirs, blank);
            n2 = 0;
            stext((short)x2, (short)y2, theirs);
        }
        if (key == 8 && n1 > 0) {
            stext((short)(x1 + n1 * 8), (short)y1, " ");
            n1 = n1 - 1;
            mine[n1] = ' ';
            stext((short)(x1 + n1 * 8), (short)y1, " ");
        }
        if (other == 8 && n2 >= 1) {
            stext((short)(x2 + n2 * 8), (short)y2, " ");
            n2 = n2 - 1;
            theirs[n2] = ' ';
            stext((short)(x2 + n2 * 8), (short)y2, " ");
        }
        if (key >= 0x20 && key <= 0xA8 && n1 <= 0x24) {
            ch[0] = key;
            ch[1] = 0;
            stext((short)(x1 + n1 * 8), (short)y1, ch);
            mine[n1++] = key;
        }
        if (other >= 0x20 && other <= 0xA8 && n2 <= 0x24) {
            ch[0] = other;
            ch[1] = 0;
            stext((short)(x2 + n2 * 8), (short)y2, ch);
            theirs[n2++] = other;
        }
        ink = 0xFF;
        border = 0xFF;
        spr_w = 8;
        spr_h = 8;
        spr_x = x1 + n1 * 8;
        spr_y = y1;
        box_draw();
        spr_x = x2 + n2 * 8;
        spr_y = y2;
        box_draw();
    }
    spr_h = sh;
    spr_w = sw;
    ink = c9;
    border = c8;
    spr_x = sx;
    spr_y = sy;
    v7ed20 = s20;
    v7ed1c = s1c;
    v7ed34 = s34;
    kbd_on();
    input_clear1();
    input_clear2();
    key88cd0 = 0;
    delay(0x18);
    page_from = 48000;
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
}

/* ---- net-synchronized menu (used by every options screen: pumps chat/keys with the peer while open) */

extern short v88bb0, v88b86, v88bb6, v88bb8, v88c80, v88c28, v88cf2;
extern short v88bee, v88bf0, v88bf2, v88c00;
extern char v88ccd, key_down, key_up, v88cd3, v88cd5, v88cd6, v88cdd, key_select;
extern char key_down, key_up, v7b6d9, v7b6db;
extern int v7ecc0, v7ecc4, v7ecac, v7ed0c, v7ea70;
extern int v7d8ac, v7d8a4;
extern char *v7d85c;
extern char *p6a978, *p6a980;
extern char *sp6a528[];
extern char mnames[];                    /* menu item names, 0x3c apart, from 0x7bc3c */

extern short v88c7c, v88c7a, v88c54, v7e794, v88b84;
extern short w7e730[], w7e584[], w7e8c4[];
extern int v7ed08, v7ea5c;
extern short v7ea58s;                     /* v7ea58 seen as short in another game source file */
extern short v7ecb0, v88c04, v88c06, v88b7a, v88b72, v7ec14;
extern int v7eca4;extern char *menu_buf6a518;              /* sp6a518 declared above */
void buttons_poll(void);
void fn_1c068(void);
void fn_1c0ac(void);

/* differs (266 rows, 228 before iter 70's behaviour fix and 233 before that: the sponsor banner's y
   is menu's own `c` (0x15f98 tests [esp+0xc8], 0x15fad passes that same slot to msgbox) and not a
   literal, which keeps `c` live to 0x15fad and moves all 20 scalar slots: +38 rows, difffn
   engines_menu now SAME. Before it: 233 rows, was 267, `isnet` is an int - the game loads it
   `movzx eax,al; mov [esp+0x84],eax` 0x1563d, ours had a 16-bit `movzx ax,al`): stack slots all
   permuted (QUESTIONS 2; the game's
   menu(n,d,mode,c) has mode@0xc4, c@0xc8, n@0xcc, d@0xd8 and 23 scalar slots, frame 0xe0; ours 20, frame
   0xd4); the game homes `four` in EBP (`mov ebp,4` 0x15640) where ours spills it; the game has ONE epilogue
   (`mov word [v88c28],0; mov eax,[esp+0xbc]; add esp,0xe0; ret` 0x163df) where our five returns get five;
   and a third inline strlen(mnames) whose result is dead (0x158d3). Block structure, calls and the switch
   jump table agree; nmrefs' one game-only 0x7bc00(mrows) reference is that 48-byte length gap (pre-existing).
   Tried: one epilogue via `goto mret` (`cursor = 1` at the two error sites) 233 -> 261; `int state` (the
   switch subject, game `cmp edx,4`) 233 -> 292; `int four` 233 -> 249. */
short menu(short n, short d, short mode, short c)
{
    char cbuf[0x48], pbuf[0x3c];        /* game slots 0x00 and 0x48; sizes make the frame 0xe0 */
    unsigned short savepal;
    short four, bounce, anim, cursor, cnt, mow, quit, tk, show13, sel, state, i;
    int w11, x0, yb, bx1, sy, ybase, d4, isnet;

    if (mode == 2)
        net7ed78 = 0;
    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
    v7ecc0 = 0;
    mow = 0;
    isnet = net7ed78 != 0;
    quit = 0;
    four = 4;
    if (v88bb0 == 1) {
        show13 = 1;                       /* slot 0xb4: ents_show skip flag */
        four = 2;
    } else
        show13 = 0;
    if (mode == 2) {
        mode = 0;
        quit = 1;
    }
    if (mode == 0) {
        if (v88bb0 == 5) {
            mow = 1;
            cursor = v88b86;
        }
        if (v88bb0 == 0xe)
            cursor = 1;
        if (v88bb0 == 0xd || v88bb0 == 0xe || v88bb0 == 0x10)
            cursor = v88c80;
        if (v88bb0 == 1)
            cursor = v88bb8;
        if (v88bb0 == 6)
            cursor = v88bb2;
        if (v88bb0 == 2) {
            mow = 1;
            cursor = v88bb6;
        }
    } else
        cursor = 1;
    if (mode != 0)
        voices_reset();
    w11 = strlen(&mnames[0]) * 11;
    x0 = (0x140 - w11) / 2;
    yb = (0xc8 - n * 12) / 2;
    if (v88bb0 == 1)
        yb += 0x1e;
    else
        yb += 0xc;
    if (mode != 0)
        yb = 0x54;
    bx1 = x0 - 0x12;
    sy = yb - 7 + (cursor - 1) * 12;
    spr_h = w11;
    spr_x = x0;
    spr_y = yb;
    w11 += 0x15;
    v88c00 = w11 + 0x14;
    v88bf0 = bx1 + w11 + 0x14;
    v88bf2 = yb - 7 + (n - 1) * 12;
    v88bee = yb - 7 + (n - 1) * 12 + 0x17 - sy;
    spr_w = n * 12;
    v88bf0 = bx1;
    v88bf2 = sy;
    ticks = 0;
    ticks2 = 0;
    v7ea70 = ticks;
    if (mode != 0) {
        sprptr = p6a980;
        spr_x = v88bf0;
        savepal = page_draw;
        spr_y = v88bf2;
        page_draw = 0;
        spr_h = v88c00;
        sprptr += 0x6400;
        spr_w = v88bee;
        spr_grab();
        page_draw = savepal;
    }
    if (mode == 0) {
        v88b4a = 0;
        p7d714 = p6a978;
        savepal = page_draw;
        pcx_blit();
        page_draw = savepal;
    }
    spr_x = 0;
    bounce = 9;
    state = mode == 0;
    tk = 0;
    if (mow == 0)
        d4 = 3;
    else
        d4 = 8;
    anim = 0;
    sel = 0;
    cnt = 0;
    for (i = 0; i < n; i++)
        wtext(x0, yb + i * 12, &mnames[i * 0x3c], 0);
    if (isnet) {
        strcpy(cbuf, " PRESS 'C' TO CHAT ");
        cbuf[7] = '"';
        cbuf[9] = '"';
        stext(center_x(strlen(cbuf) * 8), 0xb4, cbuf);
    }
    page_to = 0x7d00;
    page_from = page_draw;
    page_copy();
    spr_x = bx1;
    spr_h = 0x14;
    spr_y = sy;
    sprptr = sp6a518[sel];
    spr_w = 0x17;
    spr_draw();
    spr_x += w11;
    sprptr = sp6a528[sel];
    spr_draw();
    page_show();
    if (key88ccd)
        screenshot();
    while (key88ccd)
        ;
    if (d)
        fade_in((unsigned char *)v7d85c);
    if (page_draw)
        page_draw = 0;
    else
        page_draw = 16000;
    if (quit)
        net_end(0);
    ybase = yb - 7;
    page_from = 0x7d00;
    for (;;) {
        v7ea70 = ticks;
        if (net7ed78) {
            if (netgame == 1) {
                v7d8a4 = v7d8ac;
                ctl_setup(0);
                buttons_poll();
                if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
                    joy88b40 = 0;
                if (key_select)
                    joy88b40 = 1;
                if (key_down)
                    key88cd3 = 1;
                if (key_up)
                    key88cd6 = 1;
                if (key88cd0 == 1) {
                    v7ecc0 = 1;
                    while (key88cd0 == 1)
                        ;
                } else
                    v7ecc0 = 0;
                v88cf0 = 0;
                if (key88cd6)
                    v88cf0 = 1;
                if (key88cd3)
                    v88cf0 += 2;
                if (joy88b40)
                    v88cf0 += 4;
                if (v7ecc0)
                    v88cf0 += 0x40;
            } else {
                key88cd5 = 0;
                key88cdd = 0;
                key88cd6 = 0;
                key88cd3 = 0;
                joy88b40 = 0;
                joy88b42 = 0;
                joy88b44 = 0;
                joy88b46 = 0;
                v88cf0 = 0;
                v7ecc0 = 0;
            }
            v88cf3 = key88cd0 == 0x2e;
            net88c14 = net_key(v88cf0, v88cf3, 0);
            v7ecc4++;
            if (v7ecc4 > 1) {
                v7ecc4 = 2;
                v7ed20 = 0x88;
                v7ed1c = 0x2a8;
            }
            if (net88c14) {
                fn_1c068();
                msgbox("EXCESSIVE DATA ERRORS!", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 300)
                    ;
                fn_1c0ac();
                return 1;
            }
            if (v88cf2 == 1 || v88cf3 == 1) {
                chat();
                if (net88c14 == 0)
                    continue;
                fn_1c068();
                msgbox("EXCESSIVE DATA ERRORS!", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 300)
                    ;
                fn_1c0ac();
                return 1;
            }
            if (netgame != 1) {
                key88cd6 = 1 & v88cef;
                key88cd3 = 2 & v88cef;
                joy88b40 = v88cef & 4;
                v7ecc0 = v88cef & 0x40;
            }
        }
        if (anim == 0)
            sy = ybase + (cursor - 1) * 12;
        page_to = page_draw;
        page_copy();
        if (mode == 0)
            ents_show(bounce, 4, 1, mode, show13);
        spr_x = bx1;
        spr_h = 0x14;
        spr_y = sy;
        sprptr = sp6a518[sel];
        spr_w = 0x17;
        spr_draw();
        spr_x += w11;
        sprptr = sp6a528[sel];
        spr_draw();
        if (v88bb0 == 0xe) {
            if (cursor == 1)
                strcpy(pbuf, " PLAYER ONE ");
            else
                strcpy(pbuf, " PLAYER TWO ");
            msgbox(pbuf, 0x3c);
        }
        if (c && netgame == 0)
            /* 0x15fad `movsx edx, word [esp+0xc8]`: the sponsor banner goes at the y the caller
               handed menu (engines_menu passes 0x4e, 0x168cd), not at a constant row */
            msgbox("LEE & JACKSON LAWNMOWERS", c);
        if (net7ed78 && netgame == 2 && mode == 0)
            msgbox("REMOTE IS CONTROLLING MENU", 0x3a);
        if (v7ecac)
            msgbox("BOTH SIDES MUST USE SAME RATE", 0x3a);
        page_show();
        if (key88ccd)
            screenshot();
        while (key88ccd)
            ;
        if (page_draw)
            page_draw = 0;
        else
            page_draw = 16000;
        while (ticks - v7ea70 < 8)
            ;
        tk += 1;
        cnt++;
        switch (state) {
        case 1:
            if (--bounce == 0) {
                tk = 0;
                state = 2;
            }
            break;
        case 2:
            if (tk > 30) {
                state = 3;
                tk = 0;
            }
            break;
        case 3:
            if (++bounce > 9) {
                bounce = 9;
                state = 4;
                tk = 0;
            }
            break;
        case 4:
            for (i = 0; i < 4; i++) {
                if (four != 4 && (i == 1 || i == 3))
                    continue;
                ents[i].id = d4;
                if (mow == 0 && i >= 2)
                    ents[i].y = 7;
                else
                    ents[i].y = 0;
                d4++;
                ents[i].x = 0;
                if (mow == 0) {
                    if (d4 > 7)
                        d4 = 0;
                } else if (d4 > 0xc)
                    d4 = 5;
            }
            state = 1;
            break;
        }
        if (anim) {
            v7ed0c = 0;
            if (cnt <= 0)
                continue;
            sel++;
            if (sel > 3) {
                sel = 0;
                anim = 0;
            }
            cnt = 0;
            continue;
        }
        if (net7ed78 == 0) {
            v7d8a4 = v7d8ac;
            ctl_setup(0);
            buttons_poll();
            if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
                joy88b40 = 0;
            if (key_select)
                joy88b40 = 1;
            if (key88cd0 == 1) {
                v7ecc0 = 1;
                while (key88cd0 == 1)
                    ;
            } else
                v7ecc0 = 0;
        }
        if (v88bb0 == 1 || v88bb0 == 0x10) {
            if (v7ecc0)
                cursor = n;
        } else if (v7ecc0 && mode == 0) {
            v88c28 = 0;
            return cursor;
        }
        if (v88bb0 == 1) {
            if (joy88b40 || key88cd3 || key_down || key_up)
                v7ed0c = 0;
            else {
                v7ed0c++;
                if (v7ed0c >= 500) {
                    v88c28 = 0;
                    return cursor;
                }
            }
        }
        if (joy88b40) {
            v88c28 = 0;
            return cursor;
        }
        if (key88cd3 || key_down) {
            cursor++;
            anim = 1;
            cnt = 0;
            if (cursor > n)
                cursor = 1;
            continue;
        }
        if (key88cd6 || key_up) {
            anim = 2;
            cursor--;
            cnt = 0;
            if (cursor >= 1)
                continue;
            cursor = n;
            continue;
        }
    }
}

/* ---- the option screens that drive menu() */

extern short v7ee58, v774de, v88b9e, v88c80, detail_lvl;
extern int v7ebd4, v7ec9c, v7eca0, v7edb0, v7ecb8;
void records_load(int n);

void detail_menu(void)
{
    int r;

    strcpy(&mnames[0], " HIGH   DETAIL ");
    strcpy(&mnames[0x3c], " MEDIUM DETAIL ");
    strcpy(&mnames[0x78], " LOW    DETAIL ");
    v88c80 = v7ee58;
    v88b9e = 0;
    r = menu(3, 0, 0, 0);
    if (v7ecc0)
        v88b9e = 1;
    else
        v7ee58 = r;
}

void detail2_menu(void)
{
    int r;

    strcpy(&mnames[0], " HIGH   DETAIL ");
    strcpy(&mnames[0x3c], " MEDIUM DETAIL ");
    strcpy(&mnames[0x78], " LOW    DETAIL ");
    v88c80 = v774de;
    v88b9e = 0;
    r = menu(3, 0, 0, 0);
    if (v7ecc0)
        v88b9e = 1;
    else
        v774de = r;
}

short baud_menu(void)
{
    int r;

    v88bb0 = 0xd;
    v7ecac = 1;
    v88b9e = 0;
    switch (v7ebd4) {
    case 0x2580:
        v88c80 = 1;
        break;
    case 0x4b00:
        v88c80 = 2;
        break;
    case 0x9600:
        v88c80 = 3;
        break;
    case 0xe100:
        v88c80 = 4;
        break;
    }
    strcpy(&mnames[0], " 9600  BAUD      ");
    strcpy(&mnames[0x3c], " 19200 BAUD      ");
    strcpy(&mnames[0x78], " 38400 BAUD      ");
    strcpy(&mnames[0xb4], " 57600 BAUD      ");
    strcpy(&mnames[0xf0], " LEAVE THIS MENU ");
    r = menu(5, 0, 0, 0);
    v7ecac = 0;
    if (v7ecc0)
        v88b9e = 1;
    if (v88b9e != 0 || r == 5)
        return 0;
    switch (r) {
    case 1:
        v7ebd4 = 0x2580;
        break;
    case 2:
        v7ebd4 = 0x4b00;
        break;
    case 3:
        v7ebd4 = 0x9600;
        break;
    case 4:
        v7ebd4 = 0xe100;
        break;
    }
    return 1;
}

void zones_menu(int which, int bonus)
{
    int netok, t, r;

    v7ecb4 = 0;
    v88b9e = 0;
    netok = 0;
    if (netgame != 0) {
        if (v7ec9c == 3)
            netok = 1;
    } else {
        if (v7eca0 == 3)
            netok = 1;
    }
    if (netok == 0)
        return;
    v88bb0 = 0xd;
    t = 0;
    if (which == 2)
        t = 1;
    if (t != 0) {
        if (twoplayer != 0 || splitscreen != 0)
            if (v88b7e != 1)
                return;
    }
    v88c80 = 1;
    if (bonus != 0) {
        strcpy(&mnames[0], " REGULAR ZONES    ");
        strcpy(&mnames[0x3c], " BONUS PACK ZONES ");
    } else {
        strcpy(&mnames[0], " REGULAR RACES    ");
        strcpy(&mnames[0x3c], " BONUS PACK RACES ");
    }
    r = menu(2, 0, 0, 0);
    if (netgame && t && net88c14 != 0)
        return;
    if (v7ecc0 != 0)
        v88b9e = 1;
    if (v88b9e == 0) {
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
        if (r == 2)
            v7ecb4 = 3;
    }
}

/* differs: 32 rows, all one register home: the game keeps the value that reaches the shared tail
 * (v7ecb8/records_load/v7ee5a/v7ea58) in EDX across the three paths (`test edx,edx` 0x1685b,
 * `movsx edx,[0x7ee5a]` 0x1685f, frame 8), 9.5b puts it in [esp+8] (frame 0xc). 25 spellings in
 * three probe compiles (.work/probe_em, control 27): gotos vs if/else-if/else, declaration order
 * and `register`, `unsigned`/`long`/`enum`, the zero chained into u, the menu result through t, and
 * the zero hoisted above the early returns. `short edxv` DOES get EDX but then the ops are 16-bit
 * (`test dx,dx`) and t/u swap slots - 22 rows of a different shape, not taken. Kept: the arms
 * reassign the parameter (the game's `mov ebp,eax` is in the prologue 0x167e1, and `xor ebp,eax`
 * 0x16820 zeroes it with the still-live incoming value): 34 -> 32 rows.
 * BEHAV iter 57: nmrefs' `#0xc` ours / `#0x8` game is that frame size (`sub esp,0xc` 0x167de vs the
 * game's `sub esp,8`) - same-shape, no behaviour item. */
void engines_menu(int a)
{
    int edxv, t, u;

    v88bb0 = 0xd;
    v88b9e = 0;
    if (twoplayer != 0)
        return;
    if (netvoices != 0)
        return;
    edxv = 0;
    u = 0;
    if (a == 3) {
        edxv = 1;
        a = 0;
    }
    if (a == 2) {
        u = 1;
        a = 0;
    }
    if (a)
        t = v7ee5a;
    if (v7edb0 != 0) {
        v7ee5a = 2;
        edxv = 2;
        goto setr;
    }
    if (edxv != 0) {
        edxv = v7ee5a;
        goto setr;
    }
    if (u == 0)
        goto domenu;
    if (twoplayer == 0 && splitscreen == 0)
        goto domenu;
    if (v88b7e != 1)
        return;
domenu:
    strcpy(&mnames[0], " FAST 12 HP ENGINES ");
    strcpy(&mnames[0x3c], " SLOW 6 HP ENGINES  ");
    v88c80 = v7ee5a;
    edxv = menu(2, 0, 0, 0x4e);
    if (netgame && u != 0 && net88c14 != 0)
        return;
    if (v7ecc0 != 0)
        v88b9e = 1;
    if (v88b9e != 0)
        return;
setr:
    v7ecb8 = edxv;
    records_load(edxv);
    if (a)
        edxv = t;
    v7ee5a = edxv;
    v7ea58 = (short)edxv;
    key88cd5 = 0;
    key88cdd = 0;
    key88cd6 = 0;
    key88cd3 = 0;
    joy88b40 = 0;
    joy88b42 = 0;
    joy88b44 = 0;
    joy88b46 = 0;
}

void screen_menu(void)
{
    int sel, m;

    sel = 1;
    for (;;) {
        strcpy(&mnames[0], " SINGLE SCREEN DETAIL ");
        strcpy(&mnames[0x3c], " SPLIT SCREEN DETAIL  ");
        v88c80 = sel;
        v88b9e = 0;
        strcpy(&mnames[0x78], " LEAVE THIS MENU      ");
        v88bb0 = 0xd;
        m = menu(3, 0, 0, 0);
        sel = m;
        if (v7ecc0 != 0)
            v88b9e = 1;
        if (v88b9e != 0)
            break;
        if (m == 3)
            break;
        switch (m) {
        case 1:
            detail_menu();
            break;
        case 2:
            detail2_menu();
            break;
        default:
            break;
        }
    }
done:
    save_config(1);
    detail_lvl = v7ee58;
    v88b9e = 0;
    v7ea58 = v7ee5a;
}

short find12(short x, short y)
{
    int i;

    for (i = 0; i <= 84; i += 12) {
        if (x == *(short *)((char *)ents + i + 4) &&
            y == *(short *)((char *)ents + i + 6))
            return *(short *)((char *)ents + i);
    }
    return -1;
}

void pnames_init(void)
{
    strcpy(pnames[0], "UNO");
    strcpy(pnames[1], "SULTAN");
    strcpy(pnames[2], "MORRIS");
    strcpy(pnames[3], "PEGGLES");
    strcpy(pnames[4], "RAZER");
    strcpy(pnames[5], "RINGO");
    strcpy(pnames[6], "BLOMBO");
    strcpy(pnames[7], "TIGI");
}

void pname_desc(short idx)
{
    switch (idx) {
    case 0:
        strcpy(&mnames[0], "       UNO       ");
        strcpy(&mnames[0x3c], "  CUTE AND FAST  ");
        break;
    case 1:
        strcpy(&mnames[0], "      SULTAN     ");
        strcpy(&mnames[0x3c], " ONE HUMP OR TWO ");
        break;
    case 2:
        strcpy(&mnames[0], "      MORRIS     ");
        strcpy(&mnames[0x3c], "   OLD RELIABLE  ");
        break;
    case 3:
        strcpy(&mnames[0], "     PEGGLES     ");
        strcpy(&mnames[0x3c], "  TURBO SQUAWK!  ");
        break;
    case 4:
        strcpy(&mnames[0], "      RAZER      ");
        strcpy(&mnames[0x3c], "  KILLER WHEELS  ");
        break;
    case 5:
        strcpy(&mnames[0], "      RINGO      ");
        strcpy(&mnames[0x3c], "   RAPID RASCAL  ");
        break;
    case 6:
        strcpy(&mnames[0], "     BLOMBO      ");
        strcpy(&mnames[0x3c], "  NEVER FORGETS  ");
        break;
    case 7:
        strcpy(&mnames[0], "      TIGI       ");
        strcpy(&mnames[0x3c], "  VROOM! GROWL!  ");
        break;
    default:
        break;
    }
}

void fn_16ca0(void)
{
    short i;

    switch (detail_lvl) {
    case 1:
        v88c7c = 0x28;
        break;
    case 2:
        v88c7c = 0x24;
        break;
    case 3:
        v88c7c = 0x20;
        break;
    default:
        break;
    }
    if (twoplayer != 0 || netvoices != 0)
        v88c7c = 0x24;
    v88c54 = 0x78;
    i = v7ea58s;
    v88c7a = v88c7c >> 1;
    if (i == 2) {
        for (i = 0; i <= 199; i++)
            w7e730[i] = w7e584[i];
    } else {
        for (i = 0; i <= 199; i++)
            w7e730[i] = w7e8c4[i];
    }
    v7ea5c = v7ed08;
}

extern short arr16[];               /* 0x6AA90: per-vehicle state shorts (0x68 stride) */
extern short order_a[], order_b[];  /* 0x7D7CC / 0x7D7E0: start order tables */
extern short v70720[];              /* 0x70720 (indexed table) */

void fn_1bf00(void);

/* ---- 0x16f90, 1075 bytes: pre-race player reset (net branch first).
 * differs (185 rows, the three documented classes of QUESTIONS.md): the game CSEs zero into CX
 * and stores it 9x while -oneatx emits movw $0 per store (no C form found keeps a zero register);
 * its zeroing do-while keeps a byte counter, unreduced; the order_a/order_b loads interleave
 * between stores. New this iter: `#pragma loopopt OFF` before the definition is INERT under
 * WCC386 - a WPP pragma, and the pipeline has no preprocessor.
 * iter 60 nmrefs (all SAME, checked at the instruction): `#0x2d8` is our strength-reduced byte
 * offset of the f08/f0a loop - the game recomputes it (`movsx eax,si; imul eax,eax,0x68` 0x17319)
 * and counts the value down instead (`add edx,0x3c` 0x1730b, `sub edx,0xa` 0x1732e), which is the
 * same f08 = v88b84 / f0a = v88b84 + 10j - 10 (0x6aa98 / 0x6aa9a = f08/f0a, players base 0x6aa90);
 * the same holds for the f0c loop (0x1738a `sub edx,4` + 0x173b0 `sub edx,0xa`). The one extra
 * v88b72 load is that loop's global counter (the game stores it twice per pass, 0x1738d/0x173a2).
 * The 5-vs-3 jump-target blocks are block-boundary grouping, not missing loads: nmrefs' ref counts
 * for 0x72350, 0x72354 and 0x88c04 agree on both sides (only v88b72 differs) and the game re-reads
 * 0x88c04 twice too (0x17218, 0x17237; 0x72354 loaded once at 0x171ed, 0x72350 at 0x17203). */
void fn_16f90(void)
{
    int ax, dx, absy;
    int j, d;
    short k;
    int bx;

    if (netvoices) {
        v70720[0] = myplayer;
        for (v88b72 = 0; v88b72 <= 0x17; v88b72++) {
            k = v88b72;
            players[k].f04 = 2;
            players[k].f62 = 0;
            players[k].f54 = 0;
            players[k].f56 = 0;
            players[k].f12 = 0;
            players[k].f5c = 0;
            players[k].f58 = 0;
            players[k].f00 = 0;
            if (k <= 7) {
                players[k].f1c = 0;
                players[k].x = order_a[k];
                players[k].y = order_b[k];
                players[k].f1a = *(short *)lit72356;
            }
            j = 0;
            do {
                j += 2;
                *(short *)(lit6aab6 + j + k * 0x68) = 0;
                *(short *)(lit6aab6 + 8 + j + k * 0x68) = 0;
            } while (j != 4);
            players[k].f02 = 0;
            players[k].cell = 0;
            players[k].place = 0;
            players[k].lap = 0;
            players[k].f08 = v7e794;
        }
        fn_16f24();
        fn_1bf00();
    } else {
        if (!twoplayer) {
            j = 4;
            bx = order_a[0] + 0x50;
            for (d = 1; d <= 8; d++) {
                order_a[j] = bx;
                order_b[j] = order_b[0];
                bx -= 0x14;
                j--;
                if (j <= -1)
                    j = 7;
            }
        }
        for (v88b72 = 0; v88b72 <= 7; v88b72++) {
            k = v88b72;
            players[k].f04 = 0;
            players[k].f62 = 0;
            players[k].f54 = 0;
            players[k].f56 = 0;
            players[k].f12 = 0;
            players[k].f5c = 0;
            players[k].f58 = 0;
            players[k].f00 = 0;
            j = 0;
            do {
                j += 2;
                *(short *)(lit6aab6 + j + k * 0x68) = 0;
                *(short *)(lit6aab6 + 8 + j + k * 0x68) = 0;
            } while (j != 4);
            players[k].x = order_a[k];
            players[k].y = order_b[k];
            players[k].f1a = *(short *)lit72356;
            ax = v88c06 - players[k].y;
            if (ax < 0)
                ax = -ax;
            absy = ax;
            players[k].f1c = 0;
            dx = v88c04 - players[k].x;
            if (dx <= -1)
                dx = -dx;
            players[k].f10 = *(short *)lit72354 - absy;
            players[k].f5e = *(int *)lit72350 + absy * 4;
            if (players[k].x < v88c04)
                players[k].f00 = 3;
            if (players[k].x > v88c04)
                players[k].f00 = 4;
            players[k].f02 = 0;
            players[k].cell = 0;
            players[k].place = 0;
            players[k].lap = 0;
            players[k].f14 = dx;   /* BUG fixed iter 69 (difffn.py): the game stores CX here
                                    * (0x1727b), CX = abs(v88c04 - x) computed 0x171d0-0x171e5 and
                                    * live to that store; our C had the same abs() in dx and stored
                                    * 0. The run leaves 20/40/60/80/60/40/20 at 0x6ab0c stride 0x68
                                    * for the game and 0 for us. Read back in the race: f00 3/4
                                    * (set here from v88c04 vs x) picks dir = +-f14 and `f14 -= 4`
                                    * until it runs out (src/ww.c:13274-13281). */
            players[k].face = v70720[k];
        }
        arr16[0x173] = 60;
        arr16[0x13F] = 58;
        arr16[0x10B] = 50;
        arr16[0xD7] = 40;
        arr16[0xA3] = 34;
        arr16[0x6F] = 30;
        arr16[0x3B] = 28;
        arr16[7] = 22;
        for (j = 7; j >= 0; --j) {
            players[j].f08 = v88b84;
            players[j].f0a = v88b84 + j * 10 - 10;
        }
        arr16[3] = 24;
        arr16[0x37] = 24;
        arr16[0x6B] = 24;
        arr16[0x9F] = 24;
        arr16[0xD3] = 24;
        arr16[0x107] = 24;
        arr16[0x13B] = 24;
        arr16[0x16F] = 24;
        for (v88b72 = 7; v88b72 >= 0; v88b72--) {
            players[v88b72].f0c = v88b84 - 4 - 10 * (7 - v88b72);
        }
    }
}

struct trkline { short *pts; short n, f06; };
extern struct trkline trklines[];
#pragma aux trklines "_arr72350"
extern struct trkline *v7d7f8;

/* differs (26 rows, 324 bytes = the game's length; 22 rows are the masked-fixup artefact
 * [reg - @]/[reg + @]): the a <= 1 arm loads pts first, the game f06 and n first and pts last
 * (0x16d8b-0x16d9a); all 6 orders of the three stores, f1c stored first, f1c re-read as the index,
 * *(int *)v7d7f8 and permute.py (20 rounds) leave it. Iter 37 rewrite (58 -> 26): named fields
 * instead of WS/WI absolute addresses (the game addresses players[] through fixups), v7d7f8 itself
 * as the trkline pointer (the game caches it in ESI and stores it once at the exit), and the inner
 * refresh works on players[a] (0x16e0b `mov edx,eax`) - the old C used players[a - 1] there. */
void fn_16d68(int a, int b, int face)   /* arg regs: EAX, EDX, EBX */
{
    int i;

    if (a <= 1) {
        v7d7f8 = &trklines[(short)face];
        players[1].f1c = face;
        players[1].f1a = v7d7f8->f06;
        players[1].f10 = v7d7f8->n;
        players[1].f5e = (int)v7d7f8->pts;
    } else {
        v7d7f8 = &trklines[players[a - 1].f1c];
        players[a].f1a = v7d7f8->f06;
        players[a].f5e = players[a - 1].f5e;
        players[a].f10 = players[a - 1].f10;
        players[a].f1c = players[a - 1].f1c;
        for (i = 1; i <= b; i++) {
            players[a].x = *(short *)players[a].f5e;
            players[a].f5e += 2;
            players[a].y = *(short *)players[a].f5e;
            players[a].f5e += 2;
            if (--players[a].f10 == 0) {
                if (++players[a].f1c > v88b7a)
                    players[a].f1c = 0;
                v7d7f8 = &trklines[players[a].f1c];
                players[a].f1a = v7d7f8->f06;
                players[a].f5e = (int)v7d7f8->pts;
                players[a].f10 = v7d7f8->n;
            }
        }
    }
}

void fn_16f90(void);

/* MATCH (iter 36): nplayers is the working variable (no local copy); the game caches it in SI
 * across the function, which is what the old `n` local imitated. */
void fn_16eac(void)
{
    int cnt, i;              /* cnt is 32-bit: `cmp ebx,ecx` 0x16eec against the cached nplayers */

    cnt = 0;
    for (i = 1; i <= nplayers; i++)
        if (players[i].f02 == 2 && players[i].f56 == 4)   /* +2, not place (0x16ec5 cmp [eax+0x6aa92],2) */
            cnt++;
    if (cnt >= nplayers) {
        nplayers += 2;
        v7eca4++;
        if (nplayers > 0x12)
            nplayers = 0x12;
        fn_16f90();
    }
}

void srand(unsigned);
int rand(void);

/* differs: 2 rows of 38 instructions, one emission-order swap - the game materialises 0x104 in EDI
 * at 0x16f2f (inside the nplayers load/store pair) and loads v88b7a at 0x16f3f, ours the other way
 * round; size and every other byte agree. Tried (13 spellings, .work/probe_f24, three compiles):
 * `c = 0x104` in five source positions (all give the same swap; assigning it after the rand loop
 * loses the hoist and costs the EDI push), a volatile alias of v88b7a in the max compare and in
 * both reads (6), the max block written first (7), a temp for racetime (18), a `lim` local for the
 * bound (14), `while` and do/while and if/break forms of the i loop (14 - the game's jmp-to-test is
 * what the plain `for` gives), `short i` (23) and the 0x104 literal with the wrapper gone (16).
 * What matched the rest: 0x104 in a local (EDI, 5 pushes) and no `for(;;)`/`break` wrapper.
 * Iter 71 behaviour pass (nmrefs consts, the one-sided `+0x7a`): not a struct field at all - capstone
 * decodes it out of the 24-byte switch table our span covers after the ret (`_TEXT` fixups
 * 0x7884-0x789b, six entries 0x7a02/0x7a52/0x7a6d/0x7a86/0x7a9c/0x7ab2, every one inside fn_17928 =
 * its six switch arms). Our table sits in the same place relative to fn_17928 as the game's
 * (0x17910-0x17927, `jmp dword ptr cs:[eax*4 + 0x17910]` 0x17a82) - what differs is the public list:
 * the game has fn_16f90 (0x16f90-0x17910) between the two, our object is in source order, so the gap
 * falls in fn_16f24's span and fndiff compares our table with the game's fn_16f90 head. LAYOUT. */
void fn_16f24(void)
{
    int r, m, i, c;

    nplayers = v7ecb0;
    c = 0x104;                  /* EDI across the rand loop: `mov edx,edi` 0x16f72 */
    m = racetime;
    if (m < v88b7a)
        m = v88b7a;
    srand(m);
    do {
        r = rand();
    } while (r < 0 || r > v88b7a);
    for (i = 1; i <= nplayers; i++)
        fn_16d68(i, c, r);
}

/* differs (9 rows, 2 fake: the masked jump-table fixup 0x17a82 and the padding after 0x182cd): race-start
 * setup (0x17928). iter 39, 35 -> 9 (counted with fndiff.align; the old 171 was difflib autojunk):
 *  - `char *nb = numbuf` initialised at entry and used by itoa puts numbuf at [esp] and msg at
 *    [esp+0x50] as in the game (fixes 12 `lea edi,[esp+0x50]` rows); costs `mov ecx,esp` 0x17933 and
 *    `mov edx,ecx`. Swapping the declarations, `(void)numbuf`, `if (0) numbuf[0] = 0`, a block-local
 *    numbuf, or `nb` assigned inside the itoa block: no change; nb used in all five places: ebp frame.
 *  - the song index is the `short r` (the game's `mov ax,[v88c90]` / `mov ax,[v88b88]; dec eax; cwde`
 *    0x18265), the song size the `int edx` local (the game loads it before `cursong`).
 * Left: itoa's eax (movsx v88b88) bound before the strcpy edi/esi setup in the game, after in ours
 * (0x17c5b; all three compiler patch levels and every statement order give ours), and fread's
 * `mov eax,[v7eb00]` before `mov ecx,[datfile]` 0x182b2 (casts on both arguments: no change).
 * (The old readdat cause is gone: readdat is called through readdat_x, QUESTIONS 6.)
 * Iter 73: the iter-65 verdict below was WRONG - "byte-identical" operands without the game's fixup
 * are not relocated by DOS/4GW, so the place-icon table stayed NULL (QUESTIONS 25); now symbols.
 * Iter 65 behaviour pass (globrefs census): 0 bugs, 3 same-shape - the three one-sided data
 * addresses were our own absolute disp32s, which the census had not been reading on our side: our
 * `mov [edx+0x6e6e0],eax` 0x79ae and `mov [edx+0x6e6e4],eax` 0x79b9 are byte-identical to the
 * game's 0x17a3a/0x17a45 (the lapdata -> record-pointer table, `WI(0x6e6e0 + edx)` below), and our
 * `mov edx,[eax+0x6508e]` 0x81f0 is its 0x1828c; with the census tool fixed (GOTCHAS) this
 * function prints no one-sided address and no count gap at all. Re-measured 25 rows (nearmiss said
 * 7): 13 + 2 of them are one whole-frame swap of numbuf and msg - our msg sits at [esp] and numbuf
 * at [esp+0x50], the game the other way round, and every use flips with it (dst and src of each
 * copy pair together), so the same strings end in the same places.
 * See findings/executables.md "Open" and GOTCHAS.md. */

extern short v88bd0, v88b88, v7eb82, v88bd6, v88b4a, v88c90;
extern int v7ed98, v7ec80, v7eb00;
extern char *v7eb54;
extern char *lapdata;
/* Data addresses through linker symbols, never bare numbers: the hybrid links DGROUP at 0x60000 but
 * DOS/4GW loads it elsewhere and moves only what carries a fixup, so `WI(0x6e6e0 + edx)` wrote the
 * place-icon table at the raw link address while overlay_draw read the relocated one (QUESTIONS 25). */
extern char placeic_c[], datdir_c[], songs_c[];
#pragma aux placeic_c "_arr6e6ec"
#pragma aux datdir_c "_datdir"
#pragma aux songs_c "_arr7c530"
void fn_279e8(void);
void fn_2754c(void);
short fn_27920(int n);
void duck_screen(int after);
void fn_32e60(void);
void horizon_upload(void);              /* asm 0x36f55: 0x6400 bytes from v7eb54 to A000:7D00, four
                                   * writes per video byte with the map mask rotated 1,2,4,8
                                   * (0x36f6c-0x36f88, findings/video.md) */

void fn_17928(void)
{
    char numbuf[0x50];            /* [esp]: itoa target */
    char msg[0x50];               /* [esp+0x50]: loading message */
    short bmcn;                   /* [esp+0xa0] */
    int t;
    short r;
    int n, edx;
    int eax;
    short mp;
    char *nb = numbuf;

    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
    v7ed98 = 0;
    if (twoplayer || splitscreen) {
        v88bde = 1;
        if (netgame)
            v88bde = v88bd0;
        if (v88bde) {
            p2offset = 0x64;
            v7eb80 = 1;
            v7eb82 = 1;
            goto setdone;
        }
        p2offset = 0;
        if (netgame != 1) {
            v7eb80 = 0;
            v7eb82 = 1;
            goto setdone;
        }
    } else {
        p2offset = 0;
        v88bde = 0;
    }
    v7eb80 = 1;      /* shared tail: 0x179f5 */
    v7eb82 = 0;
setdone:;
    if (twoplayer == 0 && netvoices == 0) {
        eax = v88bde ? (int)(size_t)lapdata + 0x166e : (int)(size_t)lapdata;
        edx = 0;
        do {
            edx += 0x10;
            WI(placeic_c - 0xc + edx) = eax;  eax += 0x27e;
            WI(placeic_c - 0x8 + edx) = eax;  eax += 0x27e;
            WI(placeic_c - 0x4 + edx) = eax;  eax += 0x27e;
            WI(placeic_c + edx) = eax;  eax += 0x27e;
        } while (edx != 0x20);
    }
    switch ((short)v88bb6 + v7ecb4 - 1) {
    case 0:
        strcpy((char *)lit80ace, "BRONZEM.SP");
        v7ed98 = 1;
        track = v88b88;
        if (v7ec80) {
            strcpy((char *)lit80ace, "SILVERM.SP");
            track = 7;
        }
        break;
    case 1:
        strcpy((char *)lit80ace, "SILVERM.SP");
        track = v88b88 + 5;
        break;
    case 2:
        strcpy((char *)lit80ace, "GOLDM.SP");
        track = v88b88 + 0xa;
        break;
    case 3:
        strcpy((char *)lit80ace, "BONUSB.SP");
        track = v88b88 + 0x15;
        break;
    case 4:
        strcpy((char *)lit80ace, "BONUSS.SP");
        track = v88b88 + 0x1a;
        break;
    case 5:
        strcpy((char *)lit80ace, "BONUSG.SP");
        track = v88b88 + 0x1f;
        break;
    }
    fn_32e60();
    if (twoplayer == 0 && netvoices == 0) {
        if (v7d998) {
            if (v7d998 == 3)
                strcpy(msg, " LOADING DEMO ");
            else
                strcpy(msg, " LOADING ");
        } else {
            strcpy(msg, " LOADING");
            if (v88b88 == 5) {
                if (v7ec80) {
                    strcat(msg, " BONUS! ");
                    strcpy((char *)lit6aa68, "BONUS");
                } else {
                    strcat(msg, " FINAL ");
                    strcpy((char *)lit6aa68, "FINAL");
                }
            } else {
                itoa(v88b88, nb, 10);
                strcpy((char *)lit6aa68, "RACE ");
                strcat((char *)lit6aa68, numbuf);
                strcat(msg, " RACE ");
                strcat(msg, numbuf);
                strcat(msg, (char *)lit600a0);
            }
        }
    } else
        strcpy(msg, "LOADING ZONE");
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    msgbox(msg, 0);
    fn_279e8();
    fn_2754c();
    v88b7e = 0;
    if (twoplayer == 0 && splitscreen == 0) {
        fn_173c4();
    } else {
        fn_173c4();
        v88b7e = 1;
        mp = myplayer;            /* kept in dx across the call */
        myplayer = v88c6a;
        fn_173c4();
        myplayer = mp;
    }
    if (v7d998 == 0 || v7d998 == 3) {
        if (netvoices == 0) {
            pcx_load((char *)lit7eb6c, 2);
            palptr = pal7d850;
            getpalette();
            fade_out(pal7d850);
            v88b4a = 0;
            p7d714 = p6a980;
            page_draw = 0;
            pcx_blit();
            page_draw = 0;
        }
        if (netvoices)
            duck_screen(0);
        v88bd6 = datfind((char *)lit6a4b8);
        fread(p6a980, 0x6400, 1, datfile);
        if (v7d998 == 0 || v7d998 == 3) {
            v7eb54 = p6a980;
            horizon_upload();
        }
        page_from = 0;
        page_to = 0x3e80;
        page_copy();
        page_draw = 0;
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (v7d998 != 3 && v7ec80 == 0)
            fade_in((unsigned char *)p7d718);
    }
    v88b9e = 0;
    if (v7d998)
        goto demo;
    if (v7ec80)
        goto music;
    if (netgame == 0) {
        r = fn_27920(1);
        v88b9e = r;
        if (r)
            return;
        goto music;
    }
    save_a = page_draw;
    save_b = page_from;
    save_c = page_to;
    page_to = 0xbb80;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    if (netgame == 1) {
        msgbox("PRESS FIRE", 0);
        r = fn_27920(1);
        v88cf3 = 0;
        v88b9e = r;
        if (r)
            v88cf0 = 0x7e;
        else
            v88cf0 = 1;
        net88c14 = net_key(v88cf0, v88cf3, 0);
        if (net88c14 == 0) {
            t = ticks;
            while (ticks - t < 0x3c)
                ;
            if (v88b9e)
                return;
        }
    } else {
        msgbox("WAITING FOR PLAYER ONE TO PRESS FIRE", 0);
        net88c14 = net_key(0, 0, 0);
        t = ticks;
        while (ticks - t < 0x3c)
            ;
        if (v88cef == 0x7e && net88c14 == 0) {
            page_from = 0xbb80;
            page_draw = page_shown;
            page_to = page_shown;
            page_copy();
            page_from = save_b;
            page_to = save_c;
            page_draw = save_a;
            fn_1c068();
            msgbox("REMOTE PRESSED ESCAPE", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 0x12c)
                ;
            fn_1c0ac();
            v88b9e = 1;
            return;
        }
    }
    page_from = 0xbb80;
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
    if (net88c14 == 0)
        goto music;
    fn_1c068();
    msgbox("EXCESSIVE ERRORS!!", 0);
    ticks = 0;
    ticks2 = 0;
    while (ticks <= 0x12c)
        ;
    fn_1c0ac();
    v88b9e = 1;
    return;
demo:
    if (v7d998 == 2 || v7d998 == 3) {
        strcpy((char *)lit7d7b8, (char *)lit7d770);
        strcat((char *)lit7d7b8, ".BMC");
        datfind((char *)lit7d7b8);
        readdat(&bmcn, 2);
        readdat((char *)lit7d9a0, bmcn * 3);
    }
music:
    if (musicon && v7d998 != 2) {
        if (twoplayer || netvoices)
            r = v88c90;
        else {
            r = v88b88 - 1;
        }
        datfind(songs_c + r * 0xe);
        edx = WI(datdir_c + 0xe + datidx * 0x16);
        if (cursong == v7eb00) {
            if (MUSIC_SongPlaying())
                MUSIC_StopSong();
        }
        fread((void *)(size_t)v7eb00, edx, 1, datfile);
    }
}

/* ---- palette entry copier (0x182d0), used by the bonus menus: when a4 != 0
   copy 3*a3 bytes at dst + 3*a2 from palbase + a5 + 3*a2, else self-copy from
   dst + 3*a2 (the callers "flash" entries); callers pass a5 = pal7d84c base.
   CORRECTION: the earlier "dead code, no reference anywhere" note was WRONG,
   xref.py finds ~22 calls inside fn_1832c/fn_193a8; it is a real call in the
   game's bytes (-oe does not inline it).
   differs (near-miss, 3 rows, 1 real): the game has a dead register copy
   mov esi,ebx at 0x182d4 before imul esi,ebx,3 (+2 bytes, then everything
   shifts); every C form gets folded by 9.5b - t=a3 copy, comma, register,
   expression in both loops, plus (iter 16, one compile, 8 spellings) lim=a3
   then lim*=3, lim=a3 then lim=lim*3, short lim=(short)(a3*3), a long n=a3
   used in an `if (n == a3)` guard, long t=a3 as the bound in both loops, an
   int counter, two limit locals in the two loops. Same family as
   music_jingle/track_setup (GOTCHAS "sinks a zero argument constant").
   a4 is a WORD in the game (`test cx,cx` 0x182e9), which is why the prototype
   says short a4: that took the function from 4 diff rows to 3 with all ~24
   callers unchanged (they pass 0/1 literals). The third row is the 88-vs-92
   byte tail artifact, not a difference. */

void fn_182d0(char *dst, short a2, long a3, short a4, int a5)
{
    short lim = a3 * 3;
    long k = a2 * 3;
    char *d = dst + k;
    char *s;
    short i;

    if (a4 != 0) {
        s = (char *)(k + a5);
        for (i = 1; i <= lim; i++)
            *d++ = *s++;
    } else {
        s = dst;
        i = 1;
        while (i <= lim) {
            *d++ = *s++;
            i++;
        }
    }
}


/* ---- the bonus-races menu (0x1832c), called from 0x1bdd9 / 0x20063.
   Shows " PLEASE SEE MAIN MENU TO / FIND OUT HOW TO ORDER / THESE EXTRA RACES. "
   over the intro screen, loads SBn.PCX (B-prefixed when v7ecb4 != 0) and puts
   each PCX's palette tail (offset size-0x300) into pal7d84c; v88bb6 picks the
   race (1..3), v88b88 the sound card (1..5); netgame 1/2 exchange keys via
   net_key. Returns with p7d718 = the worked palette and v88b9e set when the
   player enters (joy88b40) or navigates out.
   differs (near-miss): (a) stack slots - the game's 7 shorts occupy all of
   0x350-0x368 (frame 0x36c) while ours spills only 4 of them (frame 0x368),
   and the arrays' card/pcx slots swap; Watcom's spill/slot choice is driven by
   its own liveness, declaration order and touch order probes (P15-P24) did not
   pin the rule; (b) [the wrapper calls are now made through aliases, QUESTIONS 6]; (c) loop counters use
   different scratch registers in the 0x1839c init loop. Strings, palette and
   net blocks reproduce the game's bytes.
   iter 60 nmrefs pass - both flagged items SAME, no bug: (i) `game only 1x 0x26ec4(pal_6bit)` is the
   -oe inline, our copy sits at 0x1853c-0x18554 (`movzbw (%eax),%dx; sar 1; sar 1; cmp 0x300,cx`) right
   where the game calls it at 0x18537; (ii) `#0xe6` (ours) vs `#0xe8` (game) is one expression, the game
   builds it as edx = v88bb6-1, eax = v88bb6 + 0xe8, eax += 2*edx (0x18710-0x1872f) = 3*v88bb6 + 0xe6.
   No reference count differs at all (every global, call and string appears equally often on both
   sides), and the 25 one-sided jump blocks are boundary grouping: each one references addresses the
   other side uses equally often (the game's `v88b9e` store + RET tail 0x19390 and its `v7e574` test
   0x18db5 are our tail store and our `if (v7e574 && v88bb6 > 1)`). Same-shape only, no racetest. */

extern int v7c658, v7ecf0, v7ec80, v7ec9c, v7e574;
extern short dac_first, dac_count, v88b88, v88c58;
extern char v88cec;
extern char *pal7d84c;
extern char p6aaae[];

extern int v684e0, v685e4;
extern char str7c854[], str7c890[], str7c8cc[];
void pal_6bit(unsigned char *p);
void wait_release(short flag);
void dac_load(void);
void trial_book(int a, int b, int show);

void fn_1832c(void)
{
    /* Watcom 9.5b's slot order for this use pattern is reproduced by declaring
       them in THIS order: pal@0 pcx@0x300 num@0x314 card@0x328 fname@0x33c.
       Probes P17-P19: 9.5b places arrays by a decl+first-use interaction, not
       plain decl or use order; the order below is what makes the slots match. */
    char pal[0x300];            /* esp+0x000: working palette */
    char pcx[0x14];             /* esp+0x300: "SB1.PCX" dat name */
    char num[0x14];             /* esp+0x314 */
    char card[0x14];            /* esp+0x328: "B" or "" */
    char fname[0x14];           /* esp+0x33c */
    /* shorts: slot order in the game is s350@0x350 ... nav@0x368; declaring the
       loop temporaries (i/up/down/ret) after the seven slot-locals reproduces it
       (arrays above: declaration order alone fixes those slots) */
    char *gp;
    short eight;
    short down, up, ret, i;
    short s350, s354, s358, card_n, s360, s364, nav;

    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
    eight = 8;
retry:
    v7ec80 = 0;
    s350 = 0;
    v684e0 = 0;
    v685e4 = 0;
    for (i = 0; i <= 7; i += 1)
        *(short *)(p6aaae + i * 0x68) = 0;
    for (i = 0; i <= 1; ++i) {
        cars[i].f5a = 0;                     /* +0x5a first, then the inc, then .face (0x183a7-0x183b1) */
        cars[i].face = 0;
    }
    if (cursong != v7c658)
        music_start(v7c658, 1);
    strcpy(str7c854, " PLEASE SEE MAIN MENU TO ");
    strcpy(str7c890, " FIND OUT HOW TO ORDER   ");
    v88c58 = 1;
    palptr = pal7d850;
    strcpy(str7c8cc, " THESE EXTRA RACES.      ");
    getpalette();
    fade_out(pal7d850);
    v88b88 = 1;
    page_draw = 0;
    pcx[0] = 0;
    if (0 == v7ecb4)
        strcpy(card, "");
    else
        strcpy(card, "B");
    strcat(pcx, card);
    strcat(pcx, "SB1.PCX");
    datfind(pcx);
    seekdat(telldat() + datsize() - 0x300);
    /* The palette goes to pal7d84c and is converted once (0x18519-0x18537: esi = [0x7d84c] for
       readdat and pal_6bit); three cards SB1-SB3, one 16000-byte page each (0x18664 cmp card_n,3 /
       jle). Found by the hybrid link: the old readdat(pcx, 0x300) wrote 768 bytes over this frame's
       20-byte pcx[] and the return address, and the zone select crashed. */
    readdat(pal7d84c, 0x300);
    pal_6bit((unsigned char *)pal7d84c);
    for (card_n = 1; card_n <= 3; card_n++) {
        itoa(card_n, num, 10);
        strcpy(fname, card);
        strcat(fname, "SB");
        strcat(fname, num);
        strcat(fname, ".PCX");
        pcx_load(fname, 2);
        v88b4a = 0;
        {
            int fs;
            p7d714 = p6a980;
            fs = page_draw;
            pcx_blit();
            page_draw = fs;
        }
        page_draw = page_draw + 16000;
    }
    page_draw = (v88bb6 - 1) * 16000;
    frame_wait();
    gp = pal7d84c;
    for (i = 0; i <= 0x2ff; ++i)
        pal[i] = gp[i];
    fn_182d0(pal, 0xe9, 1, 0, (int)gp);
    fn_182d0(pal, 0xec, 1, 0, (int)gp);
    fn_182d0(pal, 0xef, 1, 0, (int)gp);
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
    for (i = 5; i <= 9; i++)
        fn_182d0(pal, i, 1, 0, (int)gp);
    fade_in((unsigned char *)pal);
    wait_release(0);
    if (4 == v88bb2 && v7ecf0 != -1) {
        v88b88 = (short)v7ecf0;
        palptr = pal;
        dac_first = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 1, (int)gp);
        s354 = 1;
        dac_count = 1;
        dac_load();
    }
    if (netgame && net88c14) {
        for (i = 0; i <= 0x2ff; i++)
            p7d718[i] = pal[i];
        v88b9e = 1;
        return;
    }
    palptr = pal;
    v88b9e = 0;
    ticks = 0;
    ticks2 = 0;
    s354 = 0;
    s364 = 0;
mainloop:
    ticks = 0;
    page_draw = (v88bb6 - 1) * 16000;
    frame_wait();
    ticks2 = 0;
    v88cec = 0;
    while (eight > ticks) {
        if (key88cd0 == 1) {
            v88cec = 1;
            break;
        }
    }
    up = 0;
    down = 0;
    nav = 0;
    v7d8a4 = v7d8ac;
    ctl_setup(0);
    buttons_poll();
    joy_clear_y();
    if (netgame == 0) {
        if (key_select)
            joy88b40 = 1;
        if (key88cd3 || key_down)
            up = 1;
        if (key88cd6 || key_up)
            down = 1;
        if (up || down)
            nav = 1;
        goto merged;
    }
    if (netgame == 1) {
        if (key_select)
            joy88b40 = 1;
        if (key88cd3 || key_down)
            up = 1;
        if (key88cd6 || key_up)
            down = 1;
        if (up || down)
            nav = 1;
        if (up)
            key88cd3 = 1;
        if (down)
            key88cd6 = 1;
        if (netgame && joy88b40 && v7ec9c == 1 && v88bb6 > 1)
            joy88b40 = 0;
        v88cf0 = 0;
        v7ecc0 = v88cec;
        if (key88cd6)
            v88cf0 = 1;
        if (key88cd3)
            v88cf0 += 2;
        if (joy88b40)
            v88cf0 += 4;
        if (v7ecc0)
            v88cf0 += 0x40;
        v88cf3 = 0;
        net88c14 = net_key(v88cf0, v88cf3, 0);
        v7ecc4 += 1;
        if (v7ecc4 >= 2) {
            v7ecc4 = 2;
            v7ed20 = 0x88;
            v7ed1c = 0x2a8;
        }
        if (net88c14) {
            fn_1c068();
            msgbox("EXCESSIVE DATA ERRORS!", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 300)
                ;
            fn_1c0ac();
            for (i = 0; i <= 0x2ff; i++)
                p7d718[i] = pal[i];
            v88b9e = 1;
            return;
        }
    } else {
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
        v88cec = 0;
        v88cf0 = 0;
        v7ecc0 = 0;
        v88cf3 = 0;
        up = 0;
        down = 0;
        nav = 0;
        v88cf3 = 0;
        net88c14 = net_key(v88cf0, v88cf3, 0);
        v7ecc4++;
        if (v7ecc4 > 1) {
            v7ecc4 = 2;
            v7ed20 = 0x88;
            v7ed1c = 0x2a8;
        }
        if (net88c14) {
            fn_1c068();
            msgbox("EXCESSIVE DATA ERRORS!", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 300)
                ;
            fn_1c0ac();
            for (i = 0; i <= 0x2ff; i++)
                p7d718[i] = pal[i];
            v88b9e = 1;
            return;
        }
    }
    if (netgame == 2) {
        key88cd6 = v88cef & 1;
        key88cd3 = v88cef & 2;
        joy88b40 = 4 & v88cef;
        v7ecc0 = v88cef & 0x40;
        if (key88cd3)
            up = 1;
        if (key88cd6)
            down = 1;
        if (up || down)
            nav = 1;
        if (v7ecc0)
            v88cec = 1;
        else
            v88cec = 0;
    }
merged:
    if (joy88b40 || up || down || v88cec)
        wait_or_key(20);
    ++s354;
    if (s354 > 12) {
        dac_first = v88bb6 * 3 + 0xe6;
        if (s364 == 0) {
            fn_182d0(pal, dac_first, 1, 0, (int)gp);
            nav = 1;
            s364 = 1;
        } else {
            fn_182d0(pal, dac_first, 1, 1, (int)gp);
            nav = 0;
        }
        s354 = 0;
        dac_count = 1;
        palptr = pal;
        dac_load();
    }
    if (s350 != 0) {
        joy88b40 = 1;
        nav = 0;
        up = 0;
        down = 0;
    }
    if (joy88b40) {
        voice_play(0x12);
        if (4 != v88bb2)
            return;
        ret = 0;
        if (v7e574 && v88bb6 > 1) {
            voice_play(0x1a);
            ret = showlines(3, 1, 0, 1);
        }
        if (!ret)
            goto joyopen;
        s364 = 0;
        s354 = 0;
        dac_first = v88bb6 * 3 + 0xe6;
        dac_count = 1;
        fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 0, (int)gp);
        dac_load();
        goto navevaluate;
    }
    if (nav == 0) {
        if (v88cec == 0)
            goto mainloop;
        for (i = 0; i <= 0x2ff; i++)
            p7d718[i] = pal[i];
        v88b9e = 1;
        return;
    }
    s364 = 0;
    dac_first = v88bb6 * 3 + 0xe6;
    s354 = 0;
    dac_count = 1;
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 0, (int)gp);
    dac_load();
navevaluate:
    if (up) {
        v88bb6++;
        if (v88bb6 > 3)
            v88bb6 = 1;
    }
    if (down) {
        v88bb6--;
        if (v88bb6 < 1)
            v88bb6 = 3;
    }
    if (nav != 0) {
        dac_first = v88bb6 * 3 + 0xe6;
        fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
        dac_load();
    }
    if (v88cec == 0)
        goto mainloop;
    for (i = 0; i <= 0x2ff; i++)
        p7d718[i] = pal[i];
    v88b9e = 1;
    return;

joyopen:
    palptr = pal;
    dac_first = v88bb6 * 3 + 0xe6;
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
    s364 = 0;
    dac_count = 1;
    dac_load();
    dac_first = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 1, (int)gp);
    dac_count = 1;
    dac_load();
    wait_release(0);
joyloop:
    ticks = 0;
    v7ecf0 = v88b88;
    ticks = 0;
    v7d8a4 = v7d8ac;
    ticks2 = 0;
    ctl_setup(0);
    buttons_poll();
    joy_clear_y();
    if (key_select)
        joy88b40 = 1;
    for (;;) {                        /* 0x18f16: the timeout leads to the joystick navigation */
        if (ticks > eight)
            goto joynav;              /* 0x18f1c jl 0x18f8a */
        if (key88cd0 == 1)
            break;
    }
    while (key88cd0 == 1)
        ;
    palptr = pal;
    s364 = 0;
    dac_first = v88b88 + 4;
    fn_182d0(pal, 4 + v88b88, 1, 0, (int)gp);
    dac_count = 1;
    dac_load();
    goto mainloop;

joynav:                               /* 0x18f8a, reached only from the wait above (it was
                                         unreachable here until iter 38: tools/wc/nmrefs.py) */
    s358++;
    if (s358 > 12) {
        dac_first = v88b88 + 4;
        if (!s360) {
            fn_182d0(pal, 4 + v88b88, 1, 1, (int)gp);
            s360 = 1;
        } else {
            fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
            s360 = 0;
        }
        s354 = 0;
        dac_count = 1;
        palptr = pal;
        dac_load();
    }
    up = 0;
    down = 0;
    nav = 0;
    if (key88cd3 || key_down)
        up = 1;
    if (key88cd6 || key_up)
        down = 1;
    if (key88cd5 || v7b6d9)
        down = 1;
    if (key88cdd || v7b6db)
        up = 1;
    if (joy88b40 || up || down)
        wait_or_key(20);
    if (joy88b40)
        goto joyretry;
    if (nav)
        goto navevaluate;
    palptr = pal;
    dac_first = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
    dac_count = 1;
    dac_load();
    if (down) {
        v88b88--;
        if (v88b88 < 1)
            v88b88 = 5;
        dac_first = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
        dac_count = 1;
        dac_load();
    }
    if (up) {
        ++v88b88;
        if (v88b88 > 5)
            v88b88 = 1;
        dac_first = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
        dac_count = 1;
        dac_load();
    }
    goto joyloop;

joyretry:
    voice_play(0x12);
    if (v88bb2 != 4)
        return;
    v88cec = 0;
    trial_book(v88bb6 + v7ecb4, v88b88, 0);
    if (v88cec == 0)
        return;
    dac_first = v88b88 + 4;
    palptr = pal;
    fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
    dac_count = 1;
    dac_load();
    goto retry;
}


/* ---- the bonus-zones menu (0x193a8), called twice from fn_1f858 (0x20286, 0x2049e).
   Same shape as fn_1832c but the selection is a zone number v88c96 (6..0xb, wrapped
   at 0x19aa5/0x19b23) and the PCX is one file, "BSBCS.PCX" when v7ecb4 != 0 else
   "SBCS.PCX", read from the dat at size-0x300 into the palette pal7d848 points at.
   Navigation is through key88cd5/key88cdd (also fed from the cfg bytes v7b6d9/v7b6db
   and from the peer's v88cef bits when netgame == 2) instead of key88cd3/key88cd6;
   the entry voice for a bonus zone is 0x1a plus showlines(3) when v7e574 and v88c96 > 7.
   Exits set p7d718 = the worked palette and v88b9e = 1, except the joy88b40 entry
   path, which returns without them. The wait at 0x199c7 is wait_or_key's body, which
   the game has inline here (no call to 0x14c7c anywhere in this function).
   behaviour pass (iter 59, nmrefs items): every item is SAME-shape, no bug. (i) the `game more`
   set ctl_setup, buttons_poll, v7d8ac, v7d8a4, v7b6dc x2, v7b6dd and one joy88b40 store: the game
   duplicates that whole prologue-of-the-input-block in both arms (netgame == 0 at 0x19686, after
   `cmp netgame,0; jne 0x19733`, and netgame != 0 at 0x19733), we hoisted it to one copy above the
   `if (netgame == 0)` (src/ww.c:5180-5184), so each arm still runs it once; the arms' bodies agree line by line
   (0x196c0-0x1972e vs 0x1976d-0x197e5, incl. `if (netgame && v7ec9c == 1 && v88c96 > 7)` =
   0x197eb-0x19808). (ii) v88b9e `game more 1x`: our three identical exit tails (the pal -> p7d718
   copy, v88b9e = 1, return) merge into one block at our 0x19bd8 - the two early ones jump there
   (0x1962c, 0x198cc) - where the game has two copies (0x1991c, 0x19c44). (iii) pal7d848 `ours more
   1x`: the game loads it once into EBX (0x194b1) and passes it to readdat from EBX (0x194d9) and to
   pal_6bit (0x194e0); we read the global twice. (iv) `game only 1x pal_6bit` 0x194e7: -oe inlines
   pal_6bit for us (below). (v) the v7ebc0 deltas are cause (a).
   differs (near-miss): (a) the game's build caches the global v7ebc0 (connected) in EBP
   across the menu's loops - 10 loads at loop entries and 5 stores at loop exits, ~90
   bytes, none of which our 9.5b build emits for this function (it does the same dance
   for wait_or_key itself, whose body this inlines); (b) gone since iter 42 - the frame gap was the
   name buffer: `char name[0x28]` (pal 0x300 + name 0x28 + flash at esp+0x328 = the game's 0x32c; the
   code only ever writes 12 of those 40 bytes, which is why no [esp+0x30c..0x327] reference appears);
   (c) i lands in
   EDI and idx in a stack slot for us (SI/DI in the game), so the flash-toggle slot
   encodings at 0x19a19 differ; (d) the telldat/datsize/seekdat/readdat expression at
   0x194bc accumulates in EAX in the game and in EDX for us; (e) gone since iter 36 (`void pcx_blit(void)`,
   the DX save of page_draw). Blocks that do reproduce the game's bytes: the three message
   copies, the palette copy loops, the fn_182d0 flash loops, the input blocks for
   netgame 0/1/2, the net_key/v7ecc4 block, the two error/escape blocks and the two
   nav blocks. nmrefs: the one DIFFER is the game's pal_6bit call at 0x194e7, which -oe inlines for
   us; calling it through the pal_6bit_x alias was tried (nmrefs goes clean) and costs 10 rows - 148 ->
   158, ours 2048 bytes. */

extern char *pal7d848;
extern short v88c96;
extern char key_down, key_up;
void netkeys_pack(void);
void buttons_poll(void);
void dac_load(void);

void fn_193a8(void)
{
    char pal[0x300];            /* esp+0x000: working palette */
    char name[0x28];            /* esp+0x300: "(B)SBCS.PCX"; 0x28 = the game's frame (flash at esp+0x328) */
    short flash;                  /* esp+0x328: fn_182d0 a4 toggle */
    short i, idx, enter;

    v7ecc4 = 0;
    v7ed20 = 0x550;
    v7ed1c = 0x2a8;
    if (cursong != v7c658)
        music_start(v7c658, 1);
    strcpy(str7c854, " PLEASE SEE MAIN MENU TO ");
    strcpy(str7c890, " FIND OUT HOW TO ORDER   ");
    v88c58 = 1;
    palptr = pal7d850;
    strcpy(str7c8cc, " THESE EXTRA ZONES.      ");
    getpalette();
    fade_out(pal7d850);
    page_draw = 0;
    if (v7ecb4 != 0)
        strcpy(name, "BSBCS.PCX");
    else
        strcpy(name, "SBCS.PCX");
    datfind(name);
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal7d848, 0x300);
    pal_6bit(pal7d848);
    pcx_load(name, 2);
    v88b4a = 0;
    {
        short fs;
        p7d714 = p6a980;
        fs = page_draw;
        pcx_blit();
        page_draw = fs;
    }
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    for (i = 0; i <= 0x2ff; i++)
        pal[i] = pal7d848[i];
    for (i = 5; i <= 0xa; i++)
        fn_182d0(pal, i, 1, 0, (int)pal7d848);
    fn_182d0(pal, v88c96 - 1, 1, 1, (int)pal7d848);
    fade_in((unsigned char *)pal);
    flash = 0;
    wait_release(0);
    v88b9e = 0;
    ticks = 0;
    ticks2 = 0;
    dac_count = 1;
    palptr = pal;
    if (netgame && net88c14) {
        for (i = 0; i <= 0x2ff; i++)
            p7d718[i] = pal[i];
        v88b9e = 1;
        return;
    }
mainloop:
    ticks = 0;
    ticks2 = 0;
    v88cec = 0;
    while (8 > ticks) {
        if (key88cd0 == 1) {
            v88cec = 1;
            break;
        }
    }
    v7d8a4 = v7d8ac;
    ctl_setup(0);
    buttons_poll();
    if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
        joy88b40 = 0;
    if (netgame == 0) {
        if (key_select)
            joy88b40 = 1;
        if (v7b6d9)
            key88cd5 = 1;
        if (v7b6db)
            key88cdd = 1;
        if (key88cd3 || key_down)
            key88cdd = 1;
        if (key88cd6 || key_up)
            key88cd5 = 1;
        goto joined;
    }
    if (netgame == 1) {
        if (key_select)
            joy88b40 = 1;
        if (v7b6d9)
            key88cd5 = 1;
        if (v7b6db)
            key88cdd = 1;
        if (key88cd3 || key_down)
            key88cdd = 1;
        if (key88cd6 || key_up)
            key88cd5 = 1;
        if (netgame && v7ec9c == 1 && v88c96 > 7)
            joy88b40 = 0;
        netkeys_pack();
    } else {
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
        v88cec = 0;
        v88cf0 = 0;
        v88cf3 = 0;
    }
    net88c14 = net_key(v88cf0, v88cf3, 0);
    v7ecc4++;
    if (v7ecc4 > 1) {
        v7ecc4 = 2;
        v7ed20 = 0x88;
        v7ed1c = 0x220;
    }
    if (net88c14) {
        fn_1c068();
        msgbox("EXCESSIVE DATA ERRORS!", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        for (i = 0; i <= 0x2ff; i++)
            p7d718[i] = pal[i];
        v88b9e = 1;
        return;
    }
    if (netgame == 2) {
        key88cd5 = v88cef & 1;
        key88cdd = v88cef & 2;
        joy88b40 = v88cef & 4;
        joy88b42 = v88cef & 8;
        joy88b44 = v88cef & 0x10;
        joy88b46 = v88cef & 0x20;
        v88cec = v88cef & 0x40;
    }
joined:
    if (key88cd5 || key88cdd || v88cec || joy88b40) {
        ticks = 0;
        ticks2 = 0;
        do {
            if (key88cd0 == 1 && v7ebc0) {
                v7ebb0 = 1;
                break;
            }
        } while (20 > ticks);
    }
    idx = v88c96 - 1;
    i++;
    dac_first = idx;
    if (i > 12) {
        if (flash) {
            fn_182d0(pal, idx, 1, 1, (int)pal7d848);
            flash = 0;
        } else {
            fn_182d0(pal, idx, 1, 0, (int)pal7d848);
            flash = 1;
        }
        dac_load();
        i = 0;
    }
    if (key88cd5) {
        fn_182d0(pal, idx, 1, 0, (int)pal7d848);
        flash = 0;
        dac_load();
        v88c96--;
        i = 0;
        if (v88c96 < 6)
            v88c96 = 0xb;
        fn_182d0(pal, v88c96 - 1, 1, 1, (int)pal7d848);
        dac_first = v88c96 - 1;
        dac_load();
    }
    if (key88cdd) {
        fn_182d0(pal, idx, 1, 0, (int)pal7d848);
        flash = 0;
        dac_load();
        v88c96++;
        i = 0;
        if (v88c96 > 0xb)
            v88c96 = 6;
        fn_182d0(pal, v88c96 - 1, 1, 1, (int)pal7d848);
        dac_first = v88c96 - 1;
        dac_load();
    }
    if (joy88b40) {
        voice_play(0x12);
        enter = 0;
        if (netgame == 0 && v7e574 && v88c96 > 7) {
            voice_play(0x1a);
            showlines(3, 1, 0);
            enter = 1;
        }
        if (enter == 0)
            return;
    }
    if (v88cec == 0)
        goto mainloop;
    if (netgame) {
        fn_1c068();
        msgbox("ESCAPE PRESSED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
    }
    for (i = 0; i <= 0x2ff; i++)
        p7d718[i] = pal[i];
    v88b9e = 1;
}

/* ---- the car-select screen's control label (called from 0x1a437) */

void ctl_label(char *label, char *line)
{
    int num = v88b7e;
    int dev;

    if (netgame) {
        num = 0;
        if (netgame == 1) {
            if (v88b7e == 1) {
                strcpy(label, "");
                strcpy(line, "REMOTE IS CHOOSING A CAR");
                return;
            }
        } else if (v88b7e == 0) {
            strcpy(label, "");
            strcpy(line, "REMOTE IS CHOOSING A CAR");
            return;
        }
    }
    if (v88b7e == 0 || netgame != 0) {
        dev = v7ee4a;
        if (dev && ((short *) cfg77128)[0])
            dev = 2;
    } else {
        if (((short *) cfg77128)[2] == 0) {
            dev = 1;
        } else {
            dev = 0;
        }
        if (dev && ((short *) cfg77128)[1])
            dev = 2;
    }
    if (dev != 0) {
        if (dev == 1)
            strcpy(label, "JOYSTICK #");
        else
            strcpy(label, "GAME PAD #");
    } else
        strcpy(label, "KEYBOARD");
    if (dev != 0) {
        itoa(num + 1, line, 10);
        strcat(label, line);
    }
    strcpy(line, "PRESS F1 TO VIEW YOUR CONTROLS");
}

/* ---- the class line, score bonus and 0x7e730 index for the race (called from 0x3039e) */

extern int v7ece0;
extern short v88b84;
extern char cls_name[];

/* The game's jump table is at 0x19da8 (index v88bb2-1) and reads [0x19e02, 0x19e37, 0x19e6f, 0x19dde],
 * i.e. the case bodies are emitted for v88bb2 = 4,1,2,3 - which pins the labels (findings/classes.md).
 *
 * differs: one register-home flip. The game homes the local that ends in v88b84 in EDX (`mov
 * dx,[0x88b84]`, 0x19dbb) and uses EAX for the switch value (`mov ax,[0x88bb2]`, `dec eax`, `cmp
 * ax,3`, 0x19dc2); our 9.5b homes that local in EAX and puts the switch value in EDX. Every other
 * instruction of 0x19db8-0x19ea8 matches and both are 241 bytes; the swapped registers shift the jump
 * displacements. Probed without change: the local's init before/after the switch, `switch (v88bb2)`
 * with an unsigned-short global vs `(unsigned short)(v88bb2 - 1)`, short/unsigned short/int/long
 * `keep`, `register`, a comma expression inside the switch, an extra unused local, an explicit empty
 * default, per-case stores to v88b84 instead of the local (then 9.5b emits `movw $imm,disp` and no
 * entry load), both statement orders in the cases, ternary vs if/else, and every -o<letter> on top of
 * -oneatx. Same family as ctl_label's EBX-vs-EAX gap and question 1 (QUESTIONS.md 1, 2).
 * Iter 28: the homes are not arbitrary - BOTH sides put the first load in EDX and the second in EAX.
 * The game loads keep first (`mov dx,[0x88b84]` 0x19dbb) then the switch value (`mov ax,[0x88bb2]`
 * 0x19dc2); ours emits the switch's controlling expression first (`mov dx,[v88bb2]` + `dec edx`, OBJ
 * 0x9a3f) and the keep load after it (0x9a47), so the flip is the scheduler's, not the colourer's.
 * Init as its own statement, a `short`/`int` `sw` local holding v88bb2 (alone and with the init
 * statement), and `switch (v88bb2 - 1)` with renumbered cases all keep our order. */
void fn_19db8(void)
{
    short keep = v88b84;

    switch (v88bb2) {
    case 4:                      /* TIME TRIAL (0x19eac's menu): no class line */
        v7ece0 = 20;
        keep = 0x54;
        strcpy(cls_name, "");
        break;
    case 1:                      /* AMATEUR CLASS */
        v7ece0 = 8;
        keep = 0x59 + (v7ee5a != 1);
        strcpy(cls_name, "AMATEUR. ");
        break;
    case 2:                      /* PRO CLASS */
        v7ece0 = 4;
        if (v7ee5a == 1)
            keep = 0x5e;
        else
            keep = 0x5c;
        strcpy(cls_name, "PRO. ");
        break;
    case 3:                      /* CHAMPION CLASS */
        v7ece0 = 1;
        if (v7ee5a == 1)
            keep = 0x63;
        else
            keep = 0x5e;
        strcpy(cls_name, "CHAMPION. ");
        break;
    }
    v88b84 = keep;
}

/* ---- the class / game-mode menu (called from 0x1bd3c, cars_menu) */

extern int v7ecc8;

/* Five rows into mnames with v88bb0 = 6 (menu()'s cursor style); KID MODE is not a row in a net
 * game and the count passed to menu() drops to 4 with it; a two-player or net-voice game goes
 * straight to TIME TRIAL; splitscreen 2 offers only the first three rows; the KID MODE help goes
 * into the str7c854 line array, 9 lines 0x3c apart (findings/classes.md, findings/menus.md).
 *
 * differs: one instruction of 667. The game emits the store of v7edb0 = 1 at 0x1a05d - inside the
 * eighth help line's inline strcpy, after its mov ecx/mov edi/mov esi and before the rep movsd -
 * and ours at 0x1a080, just before the showlines call; every other instruction agrees (the v88bb2
 * store lands at 0x1a054 like the game's). Probed without change: the v88bb2/v7edb0 pair put before
 * each of the last six help lines and after all nine, in both orders (72 placements over four probe
 * compiles), each with the second store wrapped in a branch the optimizer folds away, and
 * `v7edb0 = v88bb2` to make the second store depend on the first: exactly one of two adjacent
 * stores is ever pulled into a strcpy's string-op unit, and which one it is follows the source
 * order (GOTCHAS, QUESTIONS.md 3). */
void classes_menu(void)
{
    short nrows;
    int two;

    two = twoplayer;
    v7edb0 = 0;
    if (!two)
        two = netvoices;
    strcpy(&mnames[0], " AMATEUR  CLASS ");
    strcpy(&mnames[0x3c], " PRO      CLASS ");
    strcpy(&mnames[0x78], " CHAMPION CLASS ");
    v88bb0 = 6;
    v88b9e = 0;
    nrows = 4;
    strcpy(&mnames[0xb4], " TIME TRIAL     ");
    if (!netgame) {
        strcpy(&mnames[0xf0], " KID MODE       ");
        nrows = 5;
    }
    v7ecf0 = -1;
    v7ecc8 = 1;
    if (!two) {                          /* two players or net voices: TIME TRIAL, no menu */
        if (!splitscreen) {
            v88bb2 = 1;
            v88bb2 = menu(nrows, 0, 0, 0);
            if (v7ecc0)
                v88b9e = 1;
            if (v88b9e)
                return;
            if (v88bb2 == 5) {           /* KID MODE: what the mode does, then drive as AMATEUR */
                strcpy(&str7c854[0], " THIS MODE IS FOR YOUNG CHILDREN ");
                strcpy(&str7c854[0x3c], " WHO MAY EXPERIENCE DIFFICULTY   ");
                strcpy(&str7c854[0x78], " CONTROLLING THE CARS.           ");
                strcpy(&str7c854[0xb4], "                                 ");
                strcpy(&str7c854[0xf0], " THE CHILD ONLY HAS TO STEER THE ");
                strcpy(&str7c854[0x12c], " VEHICLE IN THIS MODE AND DOES   ");
                strcpy(&str7c854[0x168], " NOT NEED TO ACCELERATE AS THE   ");
                strcpy(&str7c854[0x1a4], " COMPUTER FIXES THE SPEED OF THE ");
                strcpy(&str7c854[0x1e0], " CAR.                            ");
                v88bb2 = 1;
                v7edb0 = 1;
                showlines(9, 1, 0);
                if (v88b9e)
                    return;
            }
        } else {
            v88bb2 = 1;
            if (splitscreen == 2 && v88b7e == 1) {
                v88bb2 = menu(3, 0, 0, 0);
                if (v7ecc0)
                    v88b9e = 1;
                if (netgame && net88c14)
                    return;
                if (v88b9e)
                    return;
            }
        }
    } else
        v88bb2 = 4;
    if (twoplayer || splitscreen || netvoices) {
        v684e0 = 0;
        v685e4 = 0;
    }
}

/* ---- the lap-count menu (0x1a148), called from cars_menu at 0x1bd4b */

/* Three rows into mnames with v88bb0 = 0xd and the cursor on row 1 (v88c80 = 1); menu() returns a
 * 1-based row and rows 1/2/3 are SIX / EIGHT / TEN laps. A two-player or net-voice game does
 * nothing here; with the KID MODE engines locked (v7edb0 != 0) the race is 3 laps with no menu, and
 * so is a TIME TRIAL (v88bb2 == 4) outside splitscreen (there nlaps = 6); in splitscreen only the
 * player-index-1 pass shows the menu, and it drops out in a net game with net88c14 set. */
void laps_menu(void)
{
    if (twoplayer || netvoices)
        return;
    v88b9e = 0;
    if (v7edb0) {
        nlaps = 3;
        return;
    }
    strcpy(&mnames[0], " SIX LAP RACE   ");
    strcpy(&mnames[0x3c], " EIGHT LAP RACE ");
    v88bb0 = 0xd;
    v88c80 = 1;
    strcpy(&mnames[0x78], " TEN LAP RACE   ");
    if (!splitscreen) {
        if (v88bb2 != 4) {                  /* TIME TRIAL outside splitscreen: 6 laps, no menu */
            nlaps = menu(3, 0, 0, 0);
            if (v7ecc0)
                v88b9e = 1;
            if (v88b9e == 0) {
                switch (nlaps) {
                case 1: nlaps = 6; break;
                case 2: nlaps = 8; break;
                case 3: nlaps = 10; break;
                }
            }
        } else
            nlaps = 6;
        return;
    }
    if (v88b7e != 1)
        return;
    nlaps = menu(3, 0, 0, 0);
    if (v7ecc0)
        v88b9e = 1;
    if (netgame && net88c14)
        return;
    if (v88b9e)
        return;
    switch (nlaps) {
    case 1: nlaps = 6; break;
    case 2: nlaps = 8; break;
    case 3: nlaps = 10; break;
    }
}

/* ---- the setup call made just before cars_menu (0x1a2f8; called from fn_1f858 at 0x1fd7a,
 * 0x201fa and 0x20478, each immediately followed by a call to cars_menu). It clears the eight ints
 * at v7d338 through the clib filler and, when the net/local condition holds, sets slots 0, 1, 5
 * and 6 of that array to 1 (0x1a334, 0x1a33e, 0x1a348, 0x1a352).
 * fn_430f7 is not a module start: the clib module `__stos` is at 0x430be (libmatch, 165 bytes) and
 * this is its dword path 0x39 bytes in, taking eax = destination, edx = value, ecx = dword count -
 * the game loads those three at 0x1a2fa-0x1a306 and leaves ebx alone. */

extern int v7ec9c, v7e574;      /* v7d338[] is declared at 2536 */

void fn_430f7();
#pragma aux fn_430f7 parm [eax] [edx] [ecx] modify []

void fn_1a2f8(void)
{
    int ok;

    fn_430f7(v7d338, 0, 8);
    ok = 0;
    if (netgame) {
        if (v7ec9c == 1)
            ok = 1;
    } else {
        if (v7e574)
            ok = 1;
    }
    if (ok) {
        v7d338[0] = 1;
        v7d338[1] = 1;
        v7d338[5] = 1;
        v7d338[6] = 1;
    }
}

/* ---- the "CHOOSE VEHICLE" screen (0x1a380-0x1be3b, 6861 bytes: the module's largest function).
 * Frame, phases, globals and the two jump tables that sit in the gap before the function
 * (0x1a360, 0x1a370) are in findings/car_select.md.
 *
 * The 9th struct ent slot (0x73998) is the selection box: the words the notes called carbox_x /
 * carbox_y are its x and y (0x73998 + 0x64 / + 0x66 = 0x7399c / 0x7399e, set to 0xec and 0x8e at
 * 0x1a58f, 0x1a598), and the neighbours 0x739a0 / 0x739a2 are its f8 and frame, cleared at 0x1a5a1
 * and 0x1a5aa (CONFIRMED: the 0x73938 base and 0xc stride with the field displacements every other
 * ent write uses). The walks that run past the eight cars - 0x1a792 `cmp eax,0x6c` and the f2 latch
 * at 0x1b96f - cover that ninth record. */

extern short *tab6e5b0[];         /* 0x6e5b0: p7d838's source, one entry per countdown step (0x1a8c5) */
extern int hits_target;           /* 0x7eda0: the deathmatch hit target (0x1bcfb, 0x1bd03) */
extern char v7b6df[];             /* 0x7b6df: byte 3 of the 12-byte cfg control records (0x1b392) */
extern short cfg7712c;            /* 0x7712c = cfg77128 + 4 */
extern char *v7d854;              /* 0x7d854 (v7d85c is declared at 3095) */
void fn_1c0f4(char *buf);         /* 0x1c0f4: the F1 controls screen, arg1 = a caller buffer */

/* differs (431 rows, was 499: `cd15c` and `slide_up`/`slide_dn` are shorts - the game compares them
 * `cmp word ptr [esp+0x15c],9` / `cmp word ptr [esp+0x130],8`; `short slide_up, slide_dn` on their own is
 * 521, with cd15c 476). Near miss at +8 (6684 vs 6861). The statements are in the game's order - with the
 * `disp(%esp)` and the relocation bytes masked, 72 % of the disassembly rows agree - but Watcom
 * gave our 26 spilled scalars a permutation of the game's 27 slots and kept one local the game
 * spills in a register, so the frame was 4 bytes short. Probe recipe: QUESTIONS.md 5.
 * Frame: 3 char[0x50] at esp+0/0x50/0xa0, short[8] iconx/carstat at 0xf0/0x100, scalars 0x110-.
 * Not reachable: the byte-mask block 0x1aca4-0x1ad1d (the game masks v88cef's bits in AH/BH/DH/BL/DL and
 * stores bytes - our declarations of v88cef/key88cd5/v88cec are already char, so it is the register coin
 * flip), the game's `test ax,ax` after its own word store (store->load forwarding), and `short stage` for
 * the `switch (stage - 1)` subject (game `cmp ax,3` 0x1bba6): 476 -> 483.
 * iter 60 nmrefs pass - two BUGs, both invisible to every row count: (1) the block at 0x1adf0-0x1ae5d
 * draws the selection box - `spr_w = 0x1c` (0x1ae4f) and `call spr_draw` (0x1ae5d) after the sprptr
 * store, which our C never had (nmrefs' `game more spr_draw / spr_w`); (2) the caption goes to
 * print_at(capx, capy, buf1, 0): 0x1a5c6 `movsx eax,cx`, cx = the centred (0x140 - 8*strlen(buf1))/2
 * computed at 0x1a456-0x1a46b and kept live by `mov ecx,eax` 0x1a4a9 - our C passed x2e, so WCC
 * dropped the whole division. Rows 476 -> 433 (6732 vs the game's 6861), and our frame is now the
 * game's 0x17c because capx is spilled. Same-shape items: the six `game only mrows` against our three
 * mnames are the game's row-pointer base (`mov esi,0x3c; add esi,0x7bc00` 0x1a8de, `add ebp,0xb4`
 * 0x1a8e4 = mnames + 2 rows, findings/scores.md; `#0xb4` is that end); `#0x24` (game) and `#0x54`
 * (ours) are the ents byte-offset bounds of the same 4- and 7-iteration loops (0x1aa70 `cmp eax,0x24`
 * jg -> 4 records, 0x1af8a `cmp eax,0x60` jne -> 7); the one extra `in ents` is the game's `cmp`
 * after its `inc` (0x1aa59) where 9.5b reuses the flags of its own add; the two extra joy88b46 are the
 * dead zero store 0x1ab34 (buttons_poll 0x377e0 writes it, then 0x1ab55 overwrites it) and the store->load
 * re-read 0x1ad23; our extra v88b7e/twoplayer touches and the missing v88cef touches are the
 * documented byte-mask block (0x1ac8c-0x1ad1d).
 * iter 67 hand-checked the 8 switch-table arms of jt_1a360 (0x1a360, arms at 0x1b7d7..0x1bc55) with
 * `tools/wc/fixsites.py ww .work/src/WW.OBJ --arms cars_menu`, which now prints the two blocks side by
 * side: ONE BUG - `case 0` of the carstat slide switch must store `ents[i].frame = 2` at the arm head
 * (0x1b7de, unconditional: the arm's only entry is that table slot, and cx = 2 is loaded once at
 * 0x1b7b5 and reused for the arm's `carstat[i] = 2` at 0x1b80f; `case 1`'s `frame = 6` was already
 * there). The other 7 arms are same-shape: every one differs only in the `[disp(%esp)]` number, and the
 * slots pair on the same variable (col0x game 0x16c / ours 0x168 written 0x2e at 0x1a46d / 0x1a46b and
 * 0xec at 0x1a605 / 0x1a90c; curgx 0x174 / 0x160 written 0x1a574 / 0x1a572; curgy 0x170 / 0x15c written
 * 0x72 at 0x1a513 / 0x1a511; row0y 0x12c / 0x148 written 0x3a at 0x1a478 / 0x1a476), the documented
 * slot-permutation dead end. Rows 433 -> 431; RACETEST OK. */
extern char mrows_[][0x3c];
#pragma aux mrows_ "_mrows"
void cars_menu(void)
{
    char buf0[0x50], buf1[0x50], buf2[0x50];   /* esp+0x00, esp+0x50, esp+0xa0 */
    short iconx[8], carstat[8];                /* esp+0xf0, esp+0x100 */
    int c110, idle, savepal, yrow134, yrow138, yrow13c, boxy, flip, stage, boxx;
    short cd15c, slide_up, slide_dn;
    short x2e, f120, capy, cd, row0y, car, f144, longwait, move, add158, col0x, curgy, curgx, pbase;
    int y, sv, blink, ok, done, i, j;
    short capx, n;

    x2e = 0x2e;
    v7ecc4 = 0;
    v7ed20 = 0x550;
    v7ed1c = 0x2a8;
    pbase = 0;
    strcpy(buf0, "");
    strcpy(buf2, "");
    savepal = page_draw;
    strcpy(buf1, "CHOOSE VEHICLE");
    if (twoplayer != 0 || splitscreen != 0) {
        if (v88b7e == 1) {
            strcpy(buf1, "PLAYER TWO");
            pbase = 2;
        } else
            strcpy(buf1, "PLAYER ONE");
    }
    ctl_label(buf0, buf2);
    capy = 0x60;
    capx = (0x140 - (short) (8 * strlen(buf1))) / 2;   /* centred caption x: 0x1a456-0x1a46b, kept live in ECX 0x1a4a9 */
    col0x = 0x2e;
    row0y = 0x3a;
    myplayer = -1;
    v88b9e = 0;
    spr_x = 0x2e;
    spr_y = 0x3a;
    for (i = 0; i <= 3; i++) {                  /* the top row's four icons (0x1a4b1) */
        ents[i].y = 0x3a;
        ents[i].id = i;
        ents[i].frame = 0;
        carstat[i] = 0;
        ents[i].f8 = 0;
        iconx[i] = ents[i].x = spr_x;
        spr_x += 0x40;
    }
    spr_x = 0x2e;
    spr_y = 0x72;
    curgy = 0x72;
    for (i = 4; i <= 7; i++) {                  /* the bottom row's four icons (0x1a52d) */
        ents[i].frame = 0;
        ents[i].f8 = 0;
        ents[i].y = 0x72;
        ents[i].id = i;
        carstat[i] = 1;
        iconx[i] = ents[i].x = spr_x;
        if (i == 7)
            curgx = spr_x;
        spr_x += 0x40;
    }
    ents[8].x = 0xec;
    ents[8].y = 0x8e;
    ents[8].frame = 0;
    ents[8].f8 = 0;
    page_draw = 0x7d00;
    print_at(capx, capy, buf1, 0);          /* 0x1a5c6 movsx eax,cx = the centred capx, not x2e (iter 60) */
    boxy = 0x8e;
    print_at(x2e, 0xaf, buf0, 0);
    boxx = 0xec;
    print_at(x2e, 0xbc, buf2, 0);
    page_draw = savepal;
    blink = 0;
    page_from = 0x7d00;
    voice_play(0x12);

    /* --- phase 1 (0x1a637): the ten frames of each icon alternate until every ent slot of the nine
     * has a frame past 3 (0x1a783, 0x1a7a0); the two box sprites are the pbase / pbase+1 pair. */
blink:
    v7ea70 = ticks;
    page_to = page_draw;
    page_copy();
    if (ents[0].frame >= 2) {                   /* 0x1a657: nothing is drawn before frame 2 */
        for (i = 0; i < 8; i++) {
            spr_x = ents[i].x;
            spr_y = ents[i].y;
            spr_h = 0x26;
            spr_w = 0x1c;
            sprptr = faces[ents[i].id].img[0];
            if (carstat[i] != 1)
                sprptr += 0x10a0;               /* the frame-10 variant (0x1a6b8) */
            spr_draw();
        }
        spr_x = ents[8].x;
        spr_y = ents[8].y;
        spr_h = 0x26;
        spr_w = 0x1c;
        sprptr = sp6a878[pbase];
        spr_draw();
    }
    ents_draw(9);
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (page_draw == 0)                             /* 0x1a74e */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    while (ticks - v7ea70 < 8)                  /* 0x1a76f */
        ;
    blink++;
    done = 1;
    for (i = 0; i <= 8; i++) {                  /* the nine-record walk (0x1a777) */
        if (blink > 1)
            ents[i].frame++;
        if (ents[i].frame <= 3)
            done = 0;
    }
    if (blink > 1)
        blink = 0;
    if (done == 0)
        goto blink;
    ents[0].frame = 4;                          /* 0x1a7a6: the top row slides in, the bottom row waits */
    ents[0].f8 = 0;
    ents[1].frame = 4;
    ents[1].f8 = 0;
    ents[2].frame = 4;
    ents[2].f8 = 0;
    ents[3].frame = 4;
    ents[3].f8 = 0;
    ents[4].frame = 0;
    ents[4].f8 = 0;
    ents[5].frame = 0;
    ents[5].f8 = 0;
    ents[6].frame = 0;
    ents[6].f8 = 0;
    ents[7].frame = 0;
    ents[7].f8 = 0;
    longwait = 1;
    cd = 9;
    yrow134 = boxy + 4;

    /* --- phase 2 (0x1a85f): the name and description of the car under the cursor, while the bottom
     * row's frames run up to 6; find12 turns the cursor's grid point into a car id. */
nameframe:
    v7ea70 = ticks;
    car = find12(curgx, curgy);
    page_to = page_draw;
    page_copy();
    if (longwait == 0) {
        v7d878 = 0;
        p7d838 = tab6e5b0[cd];
        p7d764 = p7d838 + 1;
        v88c2e = p7d764[-1];                /* 0x1a8ea mov bx,[eax-2] */
        v88c10 = p7d764[0];                 /* 0x1a8f3 mov ax,[eax] */
        p7d838 += 2;                        /* 0x1a8f6 add dword [0x7d838],4: once per pass, not per row */
        pname_desc(car);                    /* 0x1a90d: before the row loop, called once per pass */
        y = yrow134;
        for (j = 1; j <= 2; j++) {              /* 0x1a912-0x1a932: the two name rows, 0xc apart; wtext only */
            wtext(col0x - 1, y, mrows_[j], 1);  /* index: the game's `mov esi,0x3c; add esi,mrows` 0x1a8c0 */
            y += 0xc;
        }
        cd--;
        if (cd < 0)
            cd = 0;
    }
    spr_x = ents[8].x;
    spr_h = 0x26;
    spr_w = 0x1c;
    spr_y = ents[8].y;
    sprptr = sp6a878[pbase];
    spr_draw();
    ents_show(-1, 8, 0, 0, 0);
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (page_draw == 0)                             /* 0x1a9d8 */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    if (longwait != 0) {
        while (ticks - v7ea70 < 0x3c)           /* 0x1aa04: the first frame waits 60 ticks */
            ;
        longwait = 0;
    } else
        while (ticks - v7ea70 < 0x10)           /* 0x1aa23: 16 ticks after that */
            ;
    if (ents[4].frame == 6) {                   /* 0x1aa28: the input stage starts here */
        kbd_off();
        idle = 0;
        flip = 0;
        slide_up = 0;
        slide_dn = 0;
        move = 0;
        f144 = 1;
        yrow138 = boxy + 4;
        f120 = 0;
        goto input;
    }
    ents[4].frame++;                            /* 0x1aa32: the bottom row steps once per frame */
    ents[5].frame++;
    ents[6].frame++;
    ents[7].frame++;
    for (i = 0; i < 4; i++) {
        ents[i].frame++;
        if (ents[i].frame > 7)
            ents[i].frame = 0;
    }
    goto nameframe;

    /* --- phase 3 (0x1aae0): the input frame loop. The cursor moves only while nothing is sliding;
     * a locked car clears the confirm control (0x1ac0e, 0x1b461). */
input:
    if (netgame != 0) {
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        v7d8a4 = v7d8ac;
        n = 0;
        ctl_setup(n);
        buttons_poll();
        joy88b46 = (key88cd0 == 0x3b);          /* F1 on the local keyboard (0x1ab47) */
        if (joy88b46 != 0) {
            if (v88b7e == 0 && netgame != 1)
                joy88b46 = 0;
            if (v88b7e == 1 && netgame != 2)
                joy88b46 = 0;
        }
        if (joy88b42 != 0)
            joy88b40 = 1;
        if (key_select != 0)
            joy88b40 = 1;
        if ((&v7b6d9)[12 * n] != 0)
            key88cd5 = 1;
        if ((&v7b6db)[12 * n] != 0)
            key88cdd = 1;
        v88cec = (key88cd0 == 1);
        if (v88b7e == 1 && car == v88c68) {     /* 0x1abf4: not this player's turn to pick */
            joy88b40 = 0;
            flip = 0;
            idle = 0;
        } else if (v7d338[car] != 0) {          /* 0x1ac0e: the locked cars cannot be confirmed */
            joy88b40 = 0;
            flip = 0;
            idle = 0;
        }
        netkeys_pack();
        net88c14 = net_key(v88cf0, v88cf3, 0);
        v7ecc4++;
        if (v7ecc4 > 1) {                       /* 0x1ac6c: the second and later passes go fast */
            v7ecc4 = 2;
            v7ed20 = 0x2c;
            v7ed1c = 0x220;
        }
        if ((v88b7e == 1 && netgame == 1) || (v88b7e != 1 && netgame == 2)) {
            key88cd5 = v88cef & 1;              /* the remote control byte (0x1ac8c-0x1ad1d) */
            key88cdd = v88cef & 2;
            joy88b40 = v88cef & 4;
            joy88b42 = v88cef & 8;
            joy88b44 = v88cef & 0x10;
            v88cec = v88cef & 0x40;
            joy88b46 = v88cef & 0x20;
        } else if (joy88b46 == 0)
            joy88b46 = v88cef & 0x20;
        if (net88c14 != 0) {
            fn_1c068();
            msgbox("EXCESSIVE ERRORS", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 0x12c)
                ;
            fn_1c0ac();
            return;
        }
    }
    v7ea70 = ticks;
    if (move == 0 && slide_up == 0 && slide_dn == 0)    /* 0x1ad99 */
        car = find12(curgx, curgy);
    page_to = page_draw;
    page_copy();
    ents_show(-1, 8, 0, 0, 0);
    spr_x = boxx;
    spr_h = 0x26;                 /* 0x1ae3f */
    spr_w = 0x1c;                 /* 0x1ae4f */
    spr_y = boxy;
    sprptr = sp6a878[pbase + flip];
    spr_draw();                 /* 0x1ae5d: the selection box (iter 60, found by nmrefs' spr_draw/spr_w count) */
    pname_desc(car);
    y = yrow138;
    for (j = 0; j < 2; j++) {                   /* the same two rows, unscaled (0x1ae75) */
        wtext(col0x - 1, y, &mnames[0x3c * j], 0);
        y += 0xc;
    }
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (page_draw == 0)                             /* 0x1aeb9 */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    if (move == 4) {                            /* 0x1aeb9: a slide back takes 26 ticks */
        while (ticks - v7ea70 < 0x1a)
            ;
        move = 0;
    }
    if (move != 0) {
        while (ticks - v7ea70 < 10)             /* 0x1af1b: 10 ticks per step of the slide */
            ;
        done = 0;
        for (i = 0; i < 7; i++) {               /* the top row's records plus one (0x1af29) */
            if (ents[i].y == row0y) {
                if (move == 1) {
                    ents[i].frame++;
                    if (ents[i].frame == 6)
                        done = move;
                } else {
                    ents[i].frame--;
                    if (ents[i].frame == 2)
                        done = move;
                }
            } else {
                if (move == 1) {
                    ents[i].frame--;
                    if (ents[i].frame == 2)
                        done = move;
                } else {
                    ents[i].frame++;
                    if (ents[i].frame == 6)
                        done = move;
                }
            }
        }
        if (done != 0)
            move = 4;
        goto input;
    }
    while (ticks - v7ea70 < 4)                  /* 0x1afb4 */
        ;
    idle++;
    if (idle > 4) {                             /* 0x1afc8: the box alternates only while at rest */
        if (slide_up == 0 && slide_dn == 0) {
            flip++;
            if (flip > 1)
                flip = 0;
        }
        idle = 0;
    }
    if (netgame != 0 && joy88b46 != 0) {        /* 0x1b008: F1 */
        ok = 0;
        if (v88b7e == 0 && netgame == 1)
            ok = 1;
        if (v88b7e == 1 && netgame == 2)
            ok = 1;
        if (ok != 0) {                          /* the remote is viewing help: tell it and go quiet */
            save_a = page_draw;
            save_b = page_from;
            save_c = page_to;
            page_to = 0xbb80;
            page_draw = page_shown;
            page_from = page_shown;
            page_copy();
            msgbox("REMOTE IS VIEWING HELP", 0);
            v88cec = 0;
            key88cc3 = 0;
            key88ccc = 0;
            key88cc4 = 0;
            key88cc5 = 0;
            key88cc6 = 0;
            key88cc7 = 0;
            key88cd5 = 0;
            key88cdd = 0;
            key88cd6 = 0;
            key88cd3 = 0;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            joy88b46 = 0;
            v88cf0 = 0;
            v88cf3 = 0;
            sv = v7ed20;
            v7ed20 = 0xaa0;
            net88c14 = net_key(0, 0, 0);
            if (net88c14 != 0) {
                fn_1c068();
                msgbox("EXCESSIVE ERRORS", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
                return;
            }
            while (ticks - ticks < 0x46)        /* 0x1b1ab: the game reads the volatile twice */
                ;
            v88cec = 0;
            key88cc3 = 0;
            key88ccc = 0;
            key88cc4 = 0;
            key88cc5 = 0;
            key88cc6 = 0;
            key88cc7 = 0;
            key88cd5 = 0;
            key88cdd = 0;
            key88cd6 = 0;
            key88cd3 = 0;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            joy88b46 = 0;
            page_from = 0xbb80;
            page_draw = page_shown;
            page_to = page_shown;
            page_copy();
            page_from = save_b;
            page_to = save_c;
            page_draw = save_a;
            v7ed20 = sv;
        } else {
            fn_1c0f4(buf0);                     /* 0x1b2dd: this side reads the help */
            key88cd5 = 0;
            key88cdd = 0;
            key88cd6 = 0;
            key88cd3 = 0;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            joy88b46 = 0;
            v88cec = 0;
            if (net88c14 != 0) {
                fn_1c068();
                msgbox("EXCESSIVE ERRORS", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
                return;
            }
        }
    }
    if (netgame == 0) {                         /* 0x1b272: the local controls */
        v7d8a4 = v7d8ac;
        n = v88b7e;
        ctl_setup(n);
        buttons_poll();
        if ((twoplayer != 0 || splitscreen != 0) && v88b7e == 1) {
            if (cfg7712c == 0)
                joy88b40 = joy88b44;
            else
                joy88b40 = v7b6df[12 * v88b7e];
        }
        if (joy88b42 != 0)
            joy88b40 = 1;
        if (key_select != 0)
            joy88b40 = 1;
        if ((&v7b6d9)[12 * n] != 0)
            key88cd5 = 1;
        if ((&v7b6db)[12 * n] != 0)
            key88cdd = 1;
        v88cec = (key88cd0 == 1);
        joy88b46 = (key88cd0 == 0x3b);
        if (joy88b46 != 0) {                    /* 0x1b3fa: F1 shows this player's controls */
            fn_1c0f4(buf0);
            key88cd5 = 0;
            key88cdd = 0;
            key88cd6 = 0;
            key88cd3 = 0;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            joy88b46 = 0;
            v88cec = 0;
        }
    }
    if (v7d338[car] != 0) {                     /* 0x1b461: the locked-car nag */
        flip = 0;
        idle = 0;
        if (joy88b40 != 0) {
            voice_play(0x1a);
            strcpy(str7c854, " PLEASE READ ORDERING INFO ");
            strcpy(str7c890, " TO FIND OUT HOW TO RACE   ");
            strcpy(str7c8cc, " WITH THIS ANIMAL.         ");
            showlines(3, 1, 0);
        }
    }
    if ((twoplayer != 0 || splitscreen != 0) && v88b7e == 1 && car == v88c68) {
        joy88b40 = 0;                           /* 0x1b4e1 */
        flip = 0;
        idle = 0;
    }
    if (v88cec != 0) {
        v88b9e = 1;                             /* 0x1b52e: escape leaves the menu chain */
        return;
    }
    if (joy88b40 != 0 && move == 0 && slide_dn == 0 && slide_up == 0)
        goto confirm;                           /* 0x1b576 */
    if (key88cd5 != 0 && slide_up == 0 && slide_dn == 0) {
        if (f144 == 0)
            move = 1;
        f120 = 1;
        f144 = 0;
        flip = 0;
        idle = 0;
        slide_dn = 0;
        slide_up = 8;                           /* 0x1b5ec: the 8-step slide of the top row */
        if (move != 0)
            goto input;
    }
    if (key88cdd != 0 && slide_up == 0 && slide_dn == 0) {
        if (f120 == 0)
            move = 2;
        f144 = 1;
        f120 = 0;
        flip = 0;
        idle = 0;
        slide_up = 0;
        slide_dn = 8;                           /* 0x1b676 */
        if (move != 0)
            goto input;
    }
    if (slide_up == 8 || slide_dn == 8) {       /* 0x1b6aa: tag the icons for the slide */
        for (i = 0; i < 8; i++) {
            if (ents[i].y == row0y) {
                if (slide_dn == 0) {
                    if (ents[i].x == curgx)
                        carstat[i] = 2;
                    else
                        carstat[i] = 1;
                } else {
                    if (ents[i].x == col0x)
                        carstat[i] = 2;
                    else
                        carstat[i] = 0;
                }
            }
            if (ents[i].y == curgy) {
                if (slide_dn == 0) {
                    if (ents[i].x == col0x)
                        carstat[i] = 3;
                    else
                        carstat[i] = 0;
                } else {
                    if (ents[i].x == curgx)
                        carstat[i] = 3;
                    else
                        carstat[i] = 1;
                }
            }
        }
    }
    if (slide_up != 0 || slide_dn != 0) {       /* 0x1b7ac: the slide itself, one step per frame */
        for (i = 0; i < 8; i++) {
            if (carstat[i] > 3)
                continue;
            switch (carstat[i]) {
            case 0:                             /* 0x1b7d7: sliding left out of the cursor column */
                ents[i].frame = 2;              /* 0x1b7de, unconditional at the arm head (cx = 2,
                                                   loaded once at 0x1b7b5; the arm's only entry is
                                                   table slot 0 of jt_1a360, nothing jumps to it) */
                if (ents[i].x == col0x) {
                    if (ents[i].y == curgy)
                        carstat[i] = 3;
                    else
                        carstat[i] = 2;
                } else
                    ents[i].x -= 8;
                break;
            case 1:                             /* 0x1b828: sliding right out of the cursor column */
                ents[i].frame = 6;
                if (ents[i].x == curgx)
                    carstat[i] = (ents[i].y == curgy) ? 3 : 2;
                else
                    ents[i].x += 8;
                break;
            case 2:                             /* 0x1b86e: the row under the cursor moves down */
                if (ents[i].y == curgy) {
                    if (ents[i].x == curgx) {
                        carstat[i] = 0;
                        ents[i].frame = 2;
                    } else {
                        carstat[i] = 1;
                        ents[i].frame = 6;
                    }
                } else
                    ents[i].y += 8;
                break;
            case 3:                             /* 0x1b8c1: the top row moves up */
                if (ents[i].y == row0y) {
                    if (ents[i].x == curgx) {
                        carstat[i] = 0;
                        ents[i].frame = 2;
                    } else {
                        carstat[i] = 1;
                        ents[i].frame = 6;
                    }
                } else
                    ents[i].y -= 8;
                break;
            }
        }
        if (slide_up != 0)
            slide_up--;
        if (slide_dn != 0)
            slide_dn--;
    }
    goto input;

confirm:                                    /* 0x1b939: the choice is locked in */
    add158 = 1;
    for (i = 0; i < 8; i++) {               /* the f2 latch of 0x1b946 records the slide direction */
        if (ents[i].frame == 2)
            ents[i].f2 = 2;
        if (ents[i].frame == 6)
            ents[i].f2 = 6;
    }
    voice_play(car);                        /* the chosen car's voice (0x1b987) */
    stage = 1;
    c110 = 1;
    cd15c = -1;
    yrow13c = boxy + 4;

    /* --- phase 4 (0x1b9b5): the confirm animation, a four-stage machine in `stage` (0x1be13). */
animframe:
    v7ea70 = ticks;
    page_to = page_draw;
    page_copy();
    spr_x = boxx;
    spr_h = 0x26;
    spr_w = 0x1c;
    spr_y = boxy;
    sprptr = sp6a878[pbase + add158];
    spr_draw();
    pname_desc(car);
    y = yrow13c;
    for (j = 0; j < 2; j++) {
        wtext(col0x - 1, y, &mnames[0x3c * j], 0);
        y += 0xc;
    }
    if (stage < 3)
        ents_show(cd15c, 8, 0, 0, 0);       /* 0x1ba73: the box follows the countdown */
    spr_x = curgx;
    spr_y = curgy;
    spr_h = 0x26;
    spr_w = 0x1c;
    sprptr = faces[ents[car].id].img[ents[car].frame];
    spr_draw();
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (page_draw == 0)                             /* 0x1bb16 */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    while (ticks - v7ea70 < 10)             /* 0x1bb37 */
        ;
    for (i = 0; i < 8; i++) {               /* 0x1bb48: the chosen icon cycles, the rest keep sliding */
        if (i == car) {
            ents[i].frame++;
            if (ents[i].frame > 7)
                ents[i].frame = 0;
        } else if (stage == 1) {
            if (ents[i].f2 == 2)
                ents[i].frame++;
            if (ents[i].f2 == 6)
                ents[i].frame--;
        }
    }
    switch (stage - 1) {                    /* 0x1bbbb, the jump table at 0x1a370 */
    case 0:
        c110++;
        if (c110 > 2) {
            cd15c = 0;
            stage = 2;
            c110 = 0;
        }
        break;
    case 1:
        cd15c++;
        if (cd15c > 9) {
            c110 = 0;
            cd15c = -1;
            stage = 3;
        }
        break;
    case 2:
        c110++;
        if (c110 > 5)
            stage = 4;
        break;
    case 3:                                 /* 0x1bc55: the car is chosen - the menus behind it */
        v88b9e = 0;
        ents_init();
        if (twoplayer != 0 && v88b7e == 1) {
            strcpy(&mnames[0], " PLAY FOR 5  HITS ");
            strcpy(&mnames[0x3c], " PLAY FOR 10 HITS ");
            strcpy(&mnames[0x78], " PLAY FOR 15 HITS ");
            v88b9e = 0;
            v88bb0 = 0xd;
            strcpy(&mnames[0xb4], " PLAY FOR 20 HITS ");
            v88c80 = 1;
            hits_target = menu(4, 0, 0, 0);
            hits_target = hits_target * 5;  /* 0x1bd00: lea eax,[eax+eax*4] */
            if (netgame != 0 && net88c14 != 0)
                return;
            if (v7ecc0 != 0)
                v88b9e = 1;
        }
        if (v88b9e == 0)
            classes_menu();
        if (v88b9e == 0)
            laps_menu();
        if (v88b9e == 0)
            engines_menu(2);
        if (v88b9e == 0)
            zones_menu(2, twoplayer != 0 ? twoplayer : netvoices);
        if (v88b9e == 0) {
            ok = 1;
            if (twoplayer != 0 || netvoices != 0)
                ok = 0;
            if (splitscreen != 0 && v88b7e != 1)
                ok = 0;
            if (ok != 0) {
                v7d854 = v7d85c;
                fn_1832c();
            } else if (twoplayer != 0 || netvoices != 0) {
                v88bb6 = 3;
                v88b88 = v88c96;
            }
        }
        stage = 5;
        break;
    }
    if (stage != 5)
        goto animframe;
    myplayer = car;                         /* 0x1be29 */
}

/* ---- the start-seat shuffle (0x1be3c-0x1beff), called from fn_1e2e4 at 0x1e5b9 and from
 * fn_1f858 at 0x1ffee and 0x202c9. v70720[0..7] (short) is filled with eight distinct seat
 * numbers: [0] = myplayer, [1] = v88c6a when a second player takes part, the rest from rand()
 * (single-player) or counting up with wrap (splitscreen). LIKELY: the calls and the stores. */
/* differs: near miss, 196 bytes (the game's size exactly), 10 instructions off (13 before fndiff
 * folded the masked-fixup rows, GOTCHAS iter 16/39), all but two of them one
 * same Watcom choice: it keeps a constant 0 in edi across the loop (`push edi`, `xor edi,edi`,
 * `cmp splitscreen,di`, `mov edi,ebx`, `mov edi,edx`) where the game materialises each zero
 * (`cmpw $0,splitscreen`, `xor ebx,ebx`, `xor edx,edx`). Every statement, jump and loop bound
 * otherwise agrees byte for byte; 13 C shapes (declaration orders, `register`, goto vs break vs
 * continue, do/while vs while; k1-k4 in analysis/shapes/SHAPES.C) all give the same 13. Same
 * family as QUESTIONS.md 2 and 3, and as iter 23's netkeys_out (ours homes a long-lived value in a
 * pushed register where the game rematerialises the constant: here 0 in EDI with `push/pop edi`, the
 * game `cmp word [splitscreen],0`, `xor ebx,ebx`, `xor edx,edx` and no EDI in its prologue).
 * The 10 rows today (fndiff, iter 63) are that class - `push edi` 0x1be3f, `xor edi,edi` 0x1bead,
 * `cmp di,[splitscreen]` 0x1beaf, `mov ebx,edi` 0x1bed1, `mov edx,edi` 0x1bed3, `pop edi` 0x1befb -
 * the one EAX/EDX choice at 0x1be6a (`mov ax,[myplayer]` + our store 0x1be70 vs the game's `mov dx`
 * and its store at 0x1be75) and the game's `nop` 0x1beff. The masked-fixup rows (`[eax*2 - @]` vs
 * `[eax*2 + @]`, GOTCHAS iter 16) were 3 of the 13 before fndiff folded them (iter 39).
 * Behaviour pass (iter 63), 1 item: the one-sided jump-target block is SAME-shape - the game runs the
 * two retry loops on ONE head, 0x1beae, so all three of its retry jumps re-read `splitscreen` first
 * (the rand range retries 0x1bec2 `jl 0x1beae` / 0x1bec8 `jg 0x1beae`, and the duplicate found in the
 * v70720 scan 0x1bee0 `je 0x1beae`, which skips the store 0x1beed and the post-increment 0x1beec, so
 * the same seat cd is drawn again); our build jumps back to the rand call 0x1beb8 or to the bottom
 * test 0x1bef5 `cmp cx,7; jle 0x1beaf` and also leaves cx alone, so every retry is the same. The only
 * extra effect in the game is re-reading splitscreen 0x1beae, dead: nothing between the test and the
 * call writes it and 0x43088 is the clib rand. refs 13/13 same, no one-sided const. */
void fn_1be3c(void)
{
    short i, seat, cd, cand;
    int m;

    m = racetime;
    if (m < 8)
        m = 8;
    srand(m);                           /* 0x1be4e (clib srand at 0x430ac) */
    for (i = 0; i <= 7; i++)
        v70720[i] = -1;                 /* 0x1be59 */
    v70720[0] = myplayer;               /* 0x1be75 */
    seat = 1;
    if (twoplayer != 0 || splitscreen != 0) {
        v70720[1] = v88c6a;             /* 0x1be96 */
        seat = 2;
    }
    cd = seat;                          /* 0x1bea6 (the pre-test 0x1bea8 is on seat, eax) */
    cand = -1;                          /* 0x1bea1 */
    while (cd <= 7) {                   /* 0x1beae */
        if (splitscreen == 0) {
            do {
                cand = rand();          /* 0x1beb8 (clib rand at 0x43088) */
            } while (cand < 0 || cand > 7);
        }
        cand++;                         /* 0x1beca */
        if (cand > 7)
            cand = 0;
        for (i = 0; i <= 7; i++)        /* 0x1bed5 */
            if (cand == v70720[i])
                goto again;
        v70720[cd++] = cand;            /* 0x1beed */
    again:
        ;
    }
}

extern short v7ec6c, v7ec70;            /* 0x7ec6c / 0x7ec70: read as words here; elsewhere they
 * are dwords multiplied by 0x98 (0x2b912, 0x2b950, 0x328a3) */

/* ---- per-player setup (0x1bf00-0x1bf7d), called from fn_16f90 at 0x17098. The stores are
 * CONFIRMED (players[i].f64 = 1 and players[i].face = v7ec6c for the first v7eca4 + 1 rows, max
 * 0x15, and v7ec70 for the rest, i = 1..nplayers); what the two values mean is SPECULATIVE. */
/* differs: near miss, 11 rows (was 22). The game keeps the whole store-and-continue block
 * inside each arm of the test (0x1bf36-0x1bf4c and 0x1bf5b-0x1bf6f); the per-arm shape below
 * (for(;;), exit test written per arm) now reproduces that CFG exactly, per-arm movsx+imul
 * and both cmp/jcc tails included, and puts `mov bx,[v7eca4]` on the game's byte (0x1bf0b).
 * What is left is the pushed-register mirror (GOTCHAS "coin flip"): the game keeps nplayers
 * in ESI (loaded first, 0x1bf04) and the face value in ECX (loaded inside each arm); ours
 * gives them ECX and ESI the other way round everywhere the CFG is right. 24 more shapes
 * scored (.work/n1bf/{I,J,K,K2}.C): split val into two locals, int/unsigned val, arm order
 * swap, a scratch local, the load after the clamp, ++i/do-while tests, volatile-alias reads
 * - every per-arm shape keeps the mirror; every merged shape (m1-m4, SHAPES.C) gets the
 * homes right but loses the CFG. m5 = the shape below, kept in SHAPES.C. */
void fn_1bf00(void)
{
    short n, cnt, i, val;

    n = nplayers;                       /* 0x1bf04 (the game's first load) */
    cnt = v7eca4 + 1;                   /* 0x1bf0b */
    if (cnt > 0x15)
        cnt = 0x15;
    i = 1;                              /* 0x1bf1e */
    for (;;) {
        if (cnt > 0) {                  /* 0x1bf23 */
            cnt--;                      /* 0x1bf35 */
            val = v7ec6c;               /* 0x1bf2e */
            players[i].f64 = 1;         /* 0x1bf36 */
            players[i].face = val;      /* 0x1bf40 */
            i++;
            if (i > n)                  /* 0x1bf47 */
                break;
            continue;                   /* 0x1bf4c */
        }
        val = v7ec70;                   /* 0x1bf54 */
        players[i].f64 = 1;             /* 0x1bf5b */
        players[i].face = val;          /* 0x1bf65 */
        i++;
        if (i <= n)                     /* 0x1bf6c */
            continue;
        break;
    }
    nplayers = n;                       /* 0x1bf71 */
}

/* ---- 0x1bf80, 20 bytes: the palette now on screen into pal7d850, then fade it out (callers 0x1ffa9,
 * 0x200a9 in main_menu). No `jmp` at the end: the tail call is a jump to fade_out, whose entry block
 * the compiler emits right here (fade_out is defined after pic_show, see there), so the jump goes to
 * the next instruction and is dropped. */
void fade_out(char *p);

void fade_out_screen(void)
{
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
}


/* ---- the DOS clock wrapper 0x43164: INT 21h/2Ah into a 5-byte record (stores at
 * 0x4316d-0x43179: word [p+2] = cx, [p+1] = dh, [p] = dl, [p+4] = al). */

struct dostime {
    unsigned char hund;                 /* +0 dl: hundredths of a second */
    unsigned char sec;                  /* +1 dh: seconds */
    unsigned short minhrs;              /* +2 cl = minutes, +3 ch = hours; the game reads +2 as a
                                       * word (movzx, WORD PTR [esp+0x42], 0x1ca75) */
    unsigned char dst;                  /* +4 al: daylight-saving flag */
};

extern void dos_gettime(struct dostime *t);     /* 0x43164 */

/* ---- 0x1c994: pads the string s into a fixed-width stamp. The month table (mth) is the
 * game's 48-byte const at 0x10154 ("JAN\0FEB\0..."), indexed by the seconds byte minus one;
 * what the fields mean is SPECULATIVE, every address step below is CONFIRMED from the listing.
 * The loop tail (0x1ca83-0x1cac1) is entered twice, first with the itoa buffer in esi. */
/* The loop's destination pointer is a second local (EDX/EDI) that the game re-arms from `s` (EBP)
 * after the strcat and on the back edge; with one local both loads fold into the loop head. */
void fn_1c994(char *s)                  /* arg1 = eax */
{
    char mth[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char num[16];
    struct dostime t;
    char *src;
    char c;
    char *p;

    dos_gettime(&t);                    /* 0x1c9b8 (arg eax = esp+0x40) */
    itoa(t.hund, s, 10);                /* 0x1c9bd movzx eax,BYTE [esp+0x40]; call 0x425f9 */
    if (s[1] == 0) {                    /* 0x1c9c7: a one-digit value gets a leading '0' */
        s[2] = 0;
        c = s[0];
        s[0] = '0';
        s[1] = c;
    }
    strcat(s, " ");                     /* 0x1c9db (inlined strcat, repnz scas + copy) */
    strcat(s, mth[t.sec - 1]);          /* 0x1ca08: eax = sec*4 - 4, esi = esp + eax */
    strcat(s, " ");                     /* 0x1ca45 */
    itoa(t.minhrs, num, 10);            /* 0x1ca75 movzx eax,WORD [esp+0x42]; edx = esp+0x30 */
    src = num;
    p = s;
    while (1) {
        strcat(p, src);                 /* 0x1ca83 (edi re-armed from ebp = s at 0x1caa9, 0x1cabf) */
        if (strlen(s) >= 10)            /* 0x1caa9-0x1cab8: cmp ecx,0xa; jae exit */
            return;
        src = " ";                      /* 0x1caba */
        p = s;
    }
}

extern char tbl77758[];                 /* 0x77758: two rows of 0x450 bytes = 24 records of 0x2e
                                       * (the second row 0x77ba8 is +0x450) */
extern char *v7d8a8;                    /* 0x7d8a8: record pointer, set here and at 0x1d8a8 */

/* ---- 0x1cad0: points v7d8a8 at record `rec` of table group `which` (1, 2, 3) for the current
 * row v88bb6 + v7ecb4; group 2 starts +0x170 = 8 records in, group 3 at +0x2e0 = 16. */
void fn_1cad0(int which, int rec)       /* arg1 = eax, arg2 = edx */
{
    char *p;

    rec *= 0x2e;                        /* 0x1cad0 */
    switch (which) {                    /* 0x1cad3-0x1cadd: the compare chain 2 / <2 / 3 */
    case 1:
        p = tbl77758 + (v88bb6 + v7ecb4) * 0x450;              /* 0x1cae5-0x1caf8 */
        break;
    case 2:
        p = tbl77758 + (v88bb6 + v7ecb4) * 0x450 + 0x170;      /* 0x1caff-0x1cb17 */
        break;
    case 3:
        p = tbl77758 + (v88bb6 + v7ecb4) * 0x450 + 0x2e0;      /* 0x1cb1e-0x1cb36 */
        break;
    default:
        return;                         /* 0x1cadf */
    }
    v7d8a8 = p + rec;                   /* 0x1cb3b-0x1cb40 */
}

/* ---- the F1 / controls help screen (0x1c0f4-0x1c88b, 0x798 bytes), called from cars_menu at
 * 0x1b2df and 0x1b415. Frame: sub esp,0x3d4 = twelve 80-byte slots (esp+0x000..0x3bf) plus five
 * dwords (esp+0x3c0..0x3d3); rows[0] and rows[9] are never touched, rows[10] is the two-line
 * centred buffer and rows[11] the itoa target. `0x1c66c`, reached only by the jbe at 0x1c660, is a
 * jump target inside it (findings/scores.md).
 *
 * The six control names come from 84-byte records at 0x77432, six 14-byte strings per player
 * (+0, +0xe, +0x1c, +0x2a, +0x38, +0x46), which is empty in the image and filled at runtime
 * (ctl_setup 0x14b00). `vals[3]` (esp+0x3cc) is the player index, 0 in a net game (0x1c102);
 * `vals[0]`/`vals[1]` hold 0, 1 or 2 picked from v7ee4a, cfg77128w and cfg7712a/cfg7712c
 * (0x1c183-0x1c1ea), and `vals[vals[3]]` picks which of the two applies to this side (0x1c57a,
 * 0x1c64f). findings/menus.md has the levels and the screen's layout.
 *
 * differs: 8 instructions, size now the game's 1944. iter 38: both control-name chains are
 * `switch`es with `case 0: case 1:` (the game's unsigned `cmp eax,1; jbe` + `cmp eax,2; je` +
 * `jmp`), and the two-player one sits in the else of `if (netgame == 0)` with the name pair
 * written twice: the compiler cross-jumps the copy, which is the game's shared block before the
 * netgame test with the `jbe` back into it (0x1c542, 0x1c58b): 67 -> 18 -> 14. iter 46 (14 -> 8):
 * the third centred line is `t = (int)strlen(rows[10]) * 8; x = (0x140 - t) / 2;` - the temp puts
 * `lea edx,[ecx*8]` before `mov eax,0x140` AND sinks `xor ecx,ecx` before the spr_y load like the
 * game (0x1c35c, 0x1c37a; the same expression inline at the other three sites already matches);
 * and the print loop's row pointer is `for (i = 1, q = rows[0], q += 80; ...)` - the split init
 * gives the game's `mov edi,esp; add edi,0x50` (0x1c715) where one expression gives `lea edi,
 * [esp+0x50]`. Left (both scheduling/coin flip): that pair sits 9 bytes early, above the loop's
 * `mov esi,1; cmp ebp,esi; jl` (rows[1], rows[0]+80, &rows[i][0], *(rows+i), rows[0]+i*80 -> lea;
 * the same split as two statements scores the same; an `if (n >= 1)` guard around the loop costs 9
 * rows and 1952 bytes); and the 60-tick wait keeps the snapshot in EAX and the fresh ticks in EDX
 * (0x1c821-0x1c831; i/n/t as the snapshot, declaration order, unsigned, <= 59, for/break: no
 * change). */
extern char ctl_names77432[];           /* 0x77432, inside cfg77128 (0x77128 + 0x30a) */
extern short cfg77128w, cfg7712a;       /* 0x77128, 0x7712a: shorts inside cfg77128 */
int wait_input(short mode);                     /* 0x27920: called with 0 wherever f278c4 is */

void fn_1c0f4(char *buf)                /* arg1 = eax (0x1c100: mov ebp,eax) */
{
    int vals[5];
    char rows[12][80];
    char *p, *q;
    short x;
    int i, n, t;

    if (netgame != 0)                                   /* 0x1c102 */
        vals[3] = 0;                                      /* 0x1c10c */
    else
        vals[3] = v88b7e;                                 /* 0x1c119-0x1c126 */
    spr_x = 0;                                          /* 0x1c127 */
    spr_y = 0;
    spr_h = 0x140;
    spr_w = 0xc8;
    ink = 0x2c;                                      /* 0x1c14b */
    vals[4] = page_draw;                                    /* 0x1c152-0x1c166 */
    border = 0x26;
    vals[2] = page_shown;                                    /* 0x1c167 */
    page_draw = 0xbb80;
    box_draw();                                           /* 0x1c17e */
    if (v7ee4a != 0) {                                  /* 0x1c183 */
        if (cfg77128w != 0)
            vals[0] = 2;                                /* 0x1c197 */
        else
            vals[0] = 1;                                /* 0x1c1a4 */
    } else
        vals[0] = 0;                                    /* 0x1c1b1 */
    if (cfg7712c != 0)
        vals[1] = 0;                                    /* 0x1c1c6 */
    else if (cfg7712a != 0)
        vals[1] = 2;                                    /* 0x1c1dd */
    else
        vals[1] = 1;                                    /* 0x1c1ea */
    strcpy(rows[10], "PLAYER ");                        /* 0x1c1f5-0x1c215 */
    itoa(v88b7e + 1, rows[11], 10);                     /* 0x1c225 */
    strcat(rows[10], rows[11]);                         /* 0x1c22a-0x1c24f */
    strcat(rows[10], " CONTROLS");                      /* 0x1c250-0x1c281 */
    x = (0x140 - (int)strlen(rows[10]) * 8) / 2;        /* 0x1c282-0x1c2a8 */
    spr_y = 8;                                          /* 0x1c2b6 */
    spr_x = x;                                          /* 0x1c2c6 */
    print_at(x, 8, rows[10], 0);                        /* 0x1c2d4 */
    strcpy(rows[10], "USE LEFT AND RIGHT CONTROLS TO CHOOSE");  /* 0x1c2d9 */
    x = (0x140 - (int)strlen(rows[10]) * 8) / 2;        /* 0x1c2e2-0x1c308 */
    spr_y = 0x14;                                       /* 0x1c316 */
    spr_x = x;
    print_at(x, 0x14, rows[10], 0);                     /* 0x1c334 */
    strcpy(rows[10], "THEN PRESS FIRE TO SELECT THE CAR");      /* 0x1c339 */
    spr_y = 0x1e;                                       /* 0x1c349 */
    t = (int)strlen(rows[10]) * 8;                      /* 0x1c35c: the game computes len*8 first */
    x = (0x140 - t) / 2;                                /* 0x1c352-0x1c371 */
    spr_x = x;
    print_at(x, spr_y, rows[10], 0);                    /* 0x1c38a */
    spr_x = 0x10;                                       /* 0x1c38f */
    spr_y = 0x3c;                                       /* 0x1c3a6 */
    print_at(0x10, 0x3c, buf, 0);                       /* 0x1c3af */
    p = ctl_names77432 + vals[3] * 0x54;                /* 0x1c3b4, base added at 0x1c43d */
    strcpy(rows[1], "STEER LEFT      - ");              /* 0x1c3bc */
    strcpy(rows[2], "STEER RIGHT     - ");              /* 0x1c3d4 */
    strcpy(rows[3], "ACCELERATE      - ");              /* 0x1c3ea */
    strcpy(rows[4], "FIRE WEAPONS    - ");              /* 0x1c400 */
    strcpy(rows[5], "BRAKES          - ");              /* 0x1c416 */
    strcpy(rows[6], "HAND BRAKE TURN - ");              /* 0x1c42c */
    spr_y += 0x10;                                      /* 0x1c449 */
    strcat(rows[1], p);                                 /* 0x1c451 */
    strcat(rows[2], p + 0x0e);                          /* 0x1c481 */
    strcat(rows[3], p + 0x1c);                          /* 0x1c4b1 */
    strcat(rows[4], p + 0x2a);                          /* 0x1c4e6 */
    n = 6;                                              /* 0x1c4e1 */
    if (twoplayer || splitscreen) {                     /* 0x1c50c-0x1c523 */
        p = ctl_names77432 + vals[3] * 0x54 + 0x46;     /* 0x1c532: the game recomputes the
                                                           record (0x1c524-0x1c52c) into ESI and
                                                           puts this +0x46 name in EDX, then makes
                                                           ESI the +0x38 name (q) */
        q = ctl_names77432 + vals[3] * 0x54 + 0x38;
        if (netgame == 0) {
            strcat(rows[5], q);                         /* 0x1c549 */
            strcat(rows[6], p);                         /* 0x1c5d0 */
        } else switch (vals[vals[3]]) {
        case 0:
        case 1:
            strcat(rows[5], q);
            strcat(rows[6], p);
            break;
        case 2:
            strcat(rows[5], "BUTTON3");                 /* 0x1c59e */
            strcat(rows[6], "BUTTON4");                 /* 0x1c5d0 */
            break;
        }
        strcpy(rows[7], "");                            /* 0x1c5f6-0x1c606 */
        n = 8;                                          /* 0x1c5fb */
        if (vals[3] == 0 || netgame != 0)               /* 0x1c607-0x1c619 */
            strcpy(rows[8], "F1 - F6 ARE YOUR GIGGLE KEYS!");   /* 0x1c61b */
        else                                            /* player 2 of a local game */
            strcpy(rows[8], "1  - 6  ARE YOUR GIGGLE KEYS!");   /* 0x1c635 */
    } else {
        switch (vals[vals[3]]) {                        /* 0x1c64f-0x1c667 */
        case 0:
        case 1:
            strcat(rows[5], p + 0x38);                  /* 0x1c676 */
            strcat(rows[6], p + 0x46);                  /* 0x1c6e6 */
            break;
        case 2:
            strcat(rows[5], "BUTTON3");                 /* 0x1c6b4 */
            strcat(rows[6], "BUTTON4");                 /* 0x1c6e6 */
            break;
        }
    }
    for (i = 1, q = rows[0], q += 80; i <= n; i++, q += 80) {    /* 0x1c70c-0x1c746 */
        spr_x = 0x10;
        print_at(0x10, spr_y, q, 0);
        spr_y += 0xa;
    }
    strcpy(rows[10], "PRESS FIRE TO LEAVE HELP");       /* 0x1c748 */
    spr_y = 0xba;                                       /* 0x1c763 */
    x = (0x140 - (int)strlen(rows[10]) * 8) / 2;        /* 0x1c76c-0x1c78b */
    spr_x = x;
    print_at(x, spr_y, rows[10], 0);                    /* 0x1c7a4 */
    page_show();                                      /* 0x1c7a9 */
    if (key88ccd) {                                     /* 0x1c7ae */
        screenshot();                                   /* 0x1c7b7 */
        if (key88ccd)
            do
                ;
            while (key88ccd);
    }
    while (key88cd0 == 0x3b)                            /* 0x1c7ce: wait for F1 to come up */
        ;
    wait_release(0);                                          /* 0x1c7d9 */
    if (netgame == 0) {
        wait_input(0);                                      /* 0x1c7ea */
    } else {
        v88cf0 = 0;                                     /* 0x1c7f1 */
        v88cf3 = 0;
        wait_input(0);                                      /* 0x1c801 */
        net88c14 = net_key(v88cf0, v88cf3, 0);          /* 0x1c816-0x1c81f */
        t = ticks;                                      /* 0x1c821 */
        while (ticks - t < 60)
            ;
    }
    page_draw = vals[2];                                    /* 0x1c833-0x1c83c */
    page_show();                                      /* 0x1c840 */
    if (key88ccd) {                                     /* 0x1c845 */
        screenshot();                                   /* 0x1c84e */
        if (key88ccd)
            do
                ;
            while (key88ccd);
    }
    page_shown = vals[2];                                    /* 0x1c865-0x1c87c */
    page_draw = vals[4];
}

/* ---- 0x1c88c: the same palette-page flip and tick wait as modem(), around msgbox. Nothing in
 * the image refers to it: no E8/E9 rel32 target in the code object and no absolute dword for it
 * in either object (scan, see findings/menus.md), so it is dead code the linker kept. Rebuild
 * byte-identical, fixups included. */
void fn_1c88c(char *s, int flag)        /* arg1 = eax, arg2 = edx */
{
    if (flag) {                                         /* 0x1c892 */
        save_a = page_draw;                                 /* 0x1c896 */
        save_b = page_from;
        save_c = page_to;
        page_to = 0xbb80;
        page_draw = page_shown;                                  /* 0x1c8c9 */
        page_from = page_shown;
        page_copy();                                   /* 0x1c8d5 */
    }
    msgbox(s, 0);                                       /* 0x1c8de */
    if (flag) {                                         /* 0x1c8e3 */
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        page_from = 0xbb80;                                 /* 0x1c917-0x1c92e */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();                                   /* 0x1c935 */
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
    }
}

/* ---- 0x1c968: saves the palette over pal7d850 and calls fade_out, setting 0x88bda. Also
 * unreferenced in the image (same scan as fn_1c88c). Rebuild byte-identical. */
extern short v88bda;

void fn_1c968(void)
{
    page_draw = 0;                                          /* 0x1c96d */
    palptr = (unsigned char *)pal7d850;                 /* 0x1c968, 0x1c976 */
    getpalette();                                       /* 0x1c97b */
    fade_out(pal7d850);                                   /* 0x1c985 */
    v88bda = 1;                                         /* 0x1c98a */
}

/* ---- 0x1cb44 (enter_name): the name-entry screen. arg1 = the name buffer (EBP), arg2 = the
 * heading printed centred at y 0xa (its slot 0x54), arg3 = the last index a character may go at
 * (its slot 0x50; 0xb at the score-table caller 0x1ce78, which passes the pooled empty string 0x600ac
 * as the heading and a 0x28-byte local buffer as the name). The caret sprite gn7eb2c (0xd x 0xf)
 * starts at x 0xaa + 8i, y 0x5b, and moves one step per accepted key: A-Z, space, 0-9, plus
 * backspace. Enter (0xd) and Esc (0x1b) leave through kbd_on. DOS 2509h puts the keyboard vector
 * back (0x1cc2f-0x1cc5b) before kbd_off. Called from 0x1ce78, 0x2b6d7 and 0x2e769.
 *
 * differs: 28 instructions of 709, all of them stack slots (tools/wc/fndiff.py). Ours puts `n`
 * at esp+0x50 and `heading` at 0x54 like the game, and `x` at 0x60, but the other six locals
 * permute: y 0x58/0x6c, xc 0x5c/0x58, cy 0x6c/0x5c, lim 0x64/0x68, cx 0x64/0x70, nx 0x70/0x64
 * (ours/game). 27 declaration orders probed (`.work/probes/SHAPES*.C`, tools/wc/variants.py);
 * the parameter order and the frame size follow, the permutation of the seven locals does not
 * (QUESTIONS.md 9). The two palette flips are byte-identical as if/else statements - the
 * `page_draw = page_draw ? 0 : 0x3e80` form compiles to a value select in EAX, which the game never
 * emits (GOTCHAS). SKIPped iter 31 with 0 variants: every fndiff row is a slot displacement,
 * no lever outside the class in this note. */
void enter_name(char *name, char *heading, int n)   /* arg1 = eax, arg2 = edx, arg3 = ebx */
{
    char prompt[0x28];
    union REGS r;
    struct SREGS s;
    short xc, y, cx, cy, i, nx, x, lim;     /* slots: the game's permutation, QUESTIONS.md 9 */
    char ch;

    page_to = 0x7d00;                                 /* 0x1cb55 */
    page_from = page_draw;
    page_copy();                                   /* 0x1cb6c */
    xc = center_x(strlen(heading) * 11);            /* 0x1cb74-0x1cb8c (both inlined) */
    wtext(xc, 0xa, heading, 0);                     /* 0x1cba5 */
    strcpy(prompt, "PLEASE ENTER NAME..");          /* 0x1cbbf, the literal 0x60d8c */
    y = 0x60;
    x = 0xa;
    print_at(x, y, prompt, 0);                      /* 0x1cbce */
    page_show();                                       /* 0x1cbd3 */
    if (key88ccd) {                                 /* 0x1cbd8 */
        screenshot();                               /* 0x1cbe1 */
        if (key88ccd)
            while (key88ccd)
                ;
    }
    if (page_draw == 0)                                 /* 0x1cbf8 */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    fade_in((unsigned char *)v7d85c);                                 /* 0x1cc1f */
    wait_release(0);                                      /* 0x1cc2a */
    r.x.edx = oldkbd_off;                           /* 0x1cc38 */
    r.x.eax = 0x2509;                               /* 0x1cc42 */
    s.ds = oldkbd_seg;                              /* 0x1cc4a */
    s.es = 0;                                       /* 0x1cc54 */
    int386x(0x21, &r, &r, &s);                      /* 0x1cc5b */
    kbd_off();                                      /* 0x1cc60 */
    page_from = 0x7d00;                                 /* 0x1cc65 */
    if (n > 0xb)
        strcpy(name, "                ");           /* 0x1cc81, the literal 0x60da0 */
    else
        strcpy(name, "            ");               /* 0x1cc8d, the literal 0x60db4 */
    cx = x + 0xa0;                                  /* 0x1cc91-0x1cc9a */
    lim = cx + ((n + 1) << 3);                      /* 0x1cca2-0x1ccaa, 0x1ccc2 */
    cy = y - 5;                                     /* 0x1ccac-0x1ccba */
    nx = cx;                                        /* 0x1ccbe-0x1ccc6 */
    i = 0;                                          /* EDI (0x1ccb8) */
    for (;;) {
        page_to = page_draw;                              /* 0x1ccd5 */
        page_copy();                               /* 0x1ccdf */
        sprptr = gn7eb2c;                           /* 0x1cce4 */
        spr_h = 0xf;
        spr_x = cx;
        spr_w = 0xd;
        spr_y = cy;
        spr_draw();                               /* 0x1cd16 */
        wtext(xc, 0xa, heading, 0);                 /* 0x1cd20 */
        print_at(x, y, prompt, 0);                  /* 0x1cd30 */
        print_at(nx, y, name, 0);                   /* 0x1cd40 */
        page_show();                                   /* 0x1cd45 */
        if (key88ccd) {                             /* 0x1cd4a */
            screenshot();                           /* 0x1cd53 */
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (page_draw == 0)                             /* 0x1cd6a */
            page_draw = 0x3e80;
        else
            page_draw = 0;
        ch = getch();                               /* 0x1cd88 */
        if (ch == 0xd || ch == 0x1b)                /* 0x1cd8f-0x1cd99 */
            break;
        if (ch >= 'a')
            ch -= 0x20;                             /* 0x1cda3 */
        if (ch == 8 && i > 0) {                     /* 0x1cda6-0x1cdae */
            i--;
            cx -= 8;                                /* 0x1cdb4 */
            name[i] = ' ';                          /* 0x1cdb9 */
        }
        if ((ch >= 'A' && ch <= 'Z') || ch == ' ' || (ch >= '0' && ch <= '9')) {
            if (i <= n)                             /* 0x1cde1 */
                name[i++] = ch;                     /* 0x1cde7-0x1cde8 */
            if (cx < lim)                           /* 0x1cdef */
                cx += 8;                            /* 0x1cdfa */
        }
    }
    kbd_on();                                       /* 0x1ce04 */
}

/* ---- 0x1ce14: the record book. Loads the LEAD.PCX leader board behind the name entry, asks for a
 * name, and if the run's score (v7ed5c) beats an entry of this mode's group it shifts the records
 * down and writes the new one; then fn_1d704 draws the table. Called once, at 0x2d227 in the
 * end-of-race screen 0x2bff0.
 *
 * The record fields, from the insert at 0x1d03b-0x1d0b6: +0 = the row index v88bb6 + v7ecb4,
 * +2 = myplayer, +4 = the score, +6 = the fn_1c994 stamp string and +0x1a = the enter_name name -
 * the opposite way round from the guess in findings/scores.md, which read the two 20-byte strings
 * by their order in the record. Higher scores are better: the table is scanned best-first and the
 * insert slot is the first j with v7ed5c >= sc[j] (0x1cf77); when the run beats none of the four
 * entries 0x1cf51-0x1cf57 lowers sc[3] to score-1, which puts the run in the last slot. */
extern int v7ed5c;                    /* 0x7ed5c: the score of the run just finished */
void scores_write(int n);                /* 0x3542c: builds a "WACKY" file name (strcat of 0x7c158,
                                     * itoa(n) and "WACKY"); called with v7ee5a at 0x1d0d0 */
void fn_1d704();                      /* 0x1d704: (short which = eax, int row = edx); no parameter list,
                                     * like the game's callers (see the differs: note at 0x1d704) */

struct score {                        /* 0x2e = 46 bytes, 24 per row of 0x450 */
    short row;                        /* +0 */
    short player;                     /* +2 */
    short score;                      /* +4 */
    char stamp[20];                   /* +6 */
    char name[20];                    /* +0x1a */
};

/* differs: near miss, 793 of the game's 807 bytes and 97 aligned instruction diffs
 * (tools/wc/fndiff.py). Five causes, all slot/register noise, no semantic difference:
 * 1. the game's j is in esp+0x50 and which in esp+0x54, ours the other way round (6 sites). The
 *    frame size (0x58), the array slots and each variable's own access shape (`mov dword [slot],0`
 *    / `movsx` / `inc dword` / `cmp word`) are the game's; moving the declarations does not flip
 *    the two scalar slots (GOTCHAS "does not allocate scalar stack slots in declaration order").
 *    `int k`, not `short k`, is what keeps the frame at 0x58 and puts k in ECX (0x1cf8c).
 * 2. in the three sc[] loops the game forms the row product first (0x1cea6-0x1ceb3) where ours
 *    forms i*0x2e first, and in groups 2 and 3 its eax/edx/ecx roles are swapped (11 sites).
 * 3. in the shift and insert blocks the game holds the record pointer in EAX and the 16-bit temp in
 *    EDX (0x1cfb4, 0x1d03b), ours the other way round, and its EAX died in the first inlined
 *    strcpy so the second one reloads v7d8a8 (0x1cff3/0x1cffc) where ours has one lea (20 sites);
 *    spelling the reload in the source does not bring it back.
 * 4. the game's k loop is rotated: `mov ecx,3` + `jmp` to its tail test (0x1cf8c-0x1cf91), whose j
 *    bound is recomputed with movsx+inc (0x1d01e-0x1d024); ours falls into the body and compares
 *    against a live register. The do/while form costs 157 diffs and the `for (;;)` + break form 113,
 *    this plain `for (k = 3; k > j; k--)` 97.
 * 5. (fixed iter 36: the 0x1ce57 `mov dx` is a short save of page_draw around `void pcx_blit(void)`; the
 *    switch cases at 0x1cea6-0x1cf2d re-colour with it, 98 -> 118 rows.) */

void scores_screen(void)
{
    long sc[10];                      /* esp+0x00-0x27: only sc[0..3] are read or written
                                     * (0x1cebc-0x1cf57), but the game's frame puts the next array at
                                     * esp+0x28, so its array is 40 bytes wide */
    char stamp[0x14];                 /* the game's name is at esp+0x28 (the enter_name argument, the
                                     * lea at 0x1ce68) and its stamp at esp+0x3c (the fn_1c994
                                     * argument, 0x1cf5b); declaring name first lands the two the
                                     * other way round, so the stamp is declared first */
    char name[0x14];
    short which;                      /* esp+0x50 in our build, the game's esp+0x54 (0x1ce83) */
    short j, i;                       /* j: the game's esp+0x50 (0x1cf5f); i stays in BX (0x1ceb9) */
    short k;                          /* short again (permute.py iter 56, 51 -> 44 rows with the
                                     * (v7ecb4 + v88bb6) and `better != 0` spellings); alone, short k
                                     * took a stack slot and grew the frame to 0x5c */
    int better;                       /* 0x1cf2f/0x1cf44/0x1cf4d: the game's temp is ECX, not CX */
    struct score *dst;                /* the source and the new entry are v7d8a8 itself: the game
                                     * re-reads it for each strcpy (0x1cff3, 0x1d08c; nmrefs) */

    pcx_load("LEAD.PCX", 2);     /* 0x1ce1c-0x1ce26, the string 0x60dc4 */
    palptr = pal7d850;                /* 0x1ce2b-0x1ce30 */
    getpalette();                     /* 0x1ce35 */
    fade_out(pal7d850);                 /* 0x1ce3a-0x1ce44 */
    v88b4a = 0;                       /* 0x1ce49 */
    {
        short fs;
        p7d714 = p6a980;              /* 0x1ce52-0x1ce5e */
        fs = page_draw;                   /* 0x1ce57 mov dx */
        pcx_blit();
        page_draw = fs;                   /* 0x1ce6c */
    }
    enter_name(name, "", 0xb);        /* 0x1ce68-0x1ce78: the empty heading is 0x600ac */
    which = v88bb2;                   /* 0x1ce7d-0x1ce83 */
    switch (which) {                  /* 0x1ce87-0x1ce9e: the compare chain 2 / <2 / 3; the other
                                     * cases jump straight to 0x1cf2f with sc[] untouched */
    case 1:
        for (i = 0; i <= 3; i++)      /* 0x1cea6-0x1ced1, the displacement 0x7775c */
            sc[i] = *(short *)(tbl77758 + (v88bb6 + v7ecb4) * 0x450 + i * 0x2e + 4);
        break;
    case 2:
        for (i = 0; i <= 3; i++)      /* 0x1ced5-0x1cf00, the displacement 0x778cc */
            sc[i] = *(short *)(tbl77758 + (v88bb6 + v7ecb4) * 0x450 + 0x170 + i * 0x2e + 4);
        break;
    case 3:
        for (i = 0; i <= 3; i++)      /* 0x1cf04-0x1cf2d, the displacement 0x77a3c */
            sc[i] = *(short *)(tbl77758 + (v7ecb4 + v88bb6) * 0x450 + 0x2e0 + i * 0x2e + 4);
        break;
    }
    better = 1;                       /* 0x1cf2f */
    for (i = 0; i <= 3; i++)          /* 0x1cf3c-0x1cf4b */
        if (v7ed5c >= sc[i])
            better = 0;
    if (better != 0)
        sc[3] = v7ed5c - 1;           /* 0x1cf51-0x1cf57: guarantees the loop below takes a slot */
    fn_1c994(stamp);                  /* 0x1cf5b-0x1cf67 */
    for (j = 0; j <= 3; j++) {        /* 0x1cf72, the inc test at 0x1d0b9-0x1d0c3 */
        if (v7ed5c >= sc[j]) {        /* 0x1cf77-0x1cf7a */
            if (j != 3)               /* 0x1cf80-0x1cf86 */
                for (k = 3; k > j; k--) {   /* 0x1cf8c-0x1cf91: k in ECX */
                    fn_1cad0(which, k);       /* 0x1cf9b-0x1cf9f */
                    dst = (struct score *)v7d8a8;             /* 0x1cfa9 */
                    fn_1cad0(which, k - 1);   /* 0x1cfa4-0x1cfaf */
                    dst->score = ((struct score *)v7d8a8)->score;  /* 0x1cfb9-0x1cfbd */
                    dst->row = ((struct score *)v7d8a8)->row;      /* 0x1cfc1-0x1cfc4 */
                    dst->player = ((struct score *)v7d8a8)->player;                /* 0x1cfca-0x1cfd1 */
                    strcpy(dst->name, ((struct score *)v7d8a8)->name);             /* 0x1cfd5-0x1cff2, +0x1a first */
                    strcpy(dst->stamp, ((struct score *)v7d8a8)->stamp);           /* 0x1cff3-0x1d01d, +6 second */
                }
            fn_1cad0(which, j);       /* 0x1d02c-0x1d036 */
            ((struct score *)v7d8a8)->score = v7ed5c;        /* 0x1d040-0x1d047 */
            ((struct score *)v7d8a8)->row = v88bb6 + v7ecb4; /* 0x1d04b-0x1d05d */
            ((struct score *)v7d8a8)->player = myplayer;     /* 0x1d060-0x1d06a */
            strcpy(((struct score *)v7d8a8)->stamp, stamp);  /* 0x1d06e-0x1d08b */
            strcpy(((struct score *)v7d8a8)->name, name);    /* 0x1d08c-0x1d0b6 */
            break;                    /* 0x1d0b7 */
        }
    }
    scores_write(v7ee5a);                 /* 0x1d0c9-0x1d0d0 */
    if (cursong != menusong)          /* 0x1d0d5-0x1d0e0 */
        music_start(menusong, 1);     /* 0x1d0e2-0x1d114, inlined by -oe like the game's */
    fn_1d704(v88bb2, v88bb6 + v7ecb4);                        /* 0x1d119-0x1d12d */
}

/* 0x1d13c, 516 bytes. Called from fn_1f858 at 0x2108f with the "class drivers" menu choice
 * (v88c80 1 AMATEUR / 2 PRO / 3 CHAMP, 0x21074-0x2108f; choice 4 goes to 0x1d340).
 * Each row shows that class's record table: fn_1d704(class, v7ecb4 + choice), so v7ecb4 (0 regular,
 * 3 bonus pack, zones_menu) selects the group of tbl77758. v7ecb4 != 0 prefixes the item names with
 * " BONUS" (the names are built by strcat on top of that prefix). */
/* differs: near miss, 512 of the game's 516 bytes; two Watcom choices, each one register wide.
 * (1) The game keeps the pooled "" byte of the fall-through arm in EDX (0x1d178 `mov dl,[0x600ac]`
 * then four `88 15` stores at 0x1d17e-0x1d190) and the BONUS arm's fourth strcpy in AL (load 0x1d1be
 * `a0`, store 0x1d1c7 `a2`). We get AL in both arms, 9.5b then CSEs the two loads into one value and
 * tail-merges the two fourth strcpy's, so one store at the join serves both arms (504 bytes). Writing
 * the else arm's `strcpy(&mnames[0xb4], "")` first in that arm un-merges them (512 bytes, 13 -> 12
 * rows) but puts that store at the head of the arm, 0x1d193/0x1d1a2, where the game has it at
 * 0x1d1be/0x1d1c7. Swapping the arms (`if (v7ecb4 != 0) {BONUS} else {"" x4}`) keeps four stores in
 * one arm only at the cost of moving the whole BONUS block: 504 bytes, 20 rows. (2) AL-vs-DL, 5 rows:
 * 11 probe spellings (.work/probe_rrm/R.C, control scores 6 there but 13 whole-file, so probes do not
 * rank this function) - `char c; c = *"";` plus four byte stores (folds to `mov byte [abs],0`), int c,
 * `"\0"` (copies 2 bytes), an `if (v7ecb4) {}` in the arm, both store orders in each arm - none gets
 * EDX. The sibling 0x1d340 has our merged shape in the game itself: its if arm stores three
 * (0x1d386-0x1d390) and the fourth sits at the join, 0x1d3c6, loaded into AL. */
void race_records_menu(int mode)              /* arg1 = eax (0x1d145: mov ebp,eax) */
{
    int saved = v88c80;                       /* 0x1d147-0x1d153, the esp slot restored at 0x1d32b */
    int sel = 1;                              /* EDI: menu()'s initial cursor and the choice */

    for (;;) {
        v88bb0 = 0xd;                         /* 0x1d156 */
        v88b9e = 0;                           /* 0x1d15f */
        v88c80 = sel;                         /* 0x1d168 (1 on the first pass) */
        if (v7ecb4 == 0) {                    /* 0x1d16f: cmp/jne, the "" block falls through */
            strcpy(&mnames[0], "");
            strcpy(&mnames[0x3c], "");
            strcpy(&mnames[0x78], "");
            strcpy(&mnames[0xb4], "");
        } else {
            strcpy(&mnames[0xb4], "");            /* first in this arm: the game keeps a copy of this
                                                     store in each arm (0x1d1c7), 9.5b merges them */
            strcpy(&mnames[0], " BONUS");
            strcpy(&mnames[0x3c], " BONUS");
            strcpy(&mnames[0x78], " BONUS");
        }
        strcat(&mnames[0], " BRONZE RACE     ");       /* 0x1d1cc-0x1d1fb */
        strcat(&mnames[0x3c], " SILVER RACE     ");    /* 0x1d1fc-0x1d22b */
        strcat(&mnames[0x78], " GOLD RACE       ");    /* 0x1d22c-0x1d25b */
        strcat(&mnames[0xb4], " LEAVE THIS MENU ");    /* 0x1d25c-0x1d28b */
        while (strlen(&mnames[0xb4]) < strlen(&mnames[0]))
            strcat(&mnames[0xb4], " ");                /* 0x1d28e-0x1d2dc */
        sel = menu(4, 0, 0, 0);                         /* 0x1d2de-0x1d2ee, movsx EDI,AX */
        if (v7ecc0 != 0)                               /* 0x1d2f1 */
            v88b9e = 1;
        if (v88b9e != 0)                               /* 0x1d303 */
            sel = 4;
        if (sel == 4)                                  /* 0x1d312 */
            break;
        fn_1d704(mode, v7ecb4 + sel);                  /* 0x1d317-0x1d321 */
    }
    v88c80 = saved;                                    /* 0x1d32b-0x1d331 */
}

extern char mrows[][0x3c];       /* 0x7bc00: the menu rows indexed from 1, mrows[1] == mnames (0x1d579:
                                   * mov ebp,0x3c + add ebp,0x7bc00 is i*0x3c + &mrows[0]; spelling it
                                   * &mnames[(i-1)*0x3c] gives mov ebp,mnames and 38 diffs) */

/* 0x1d340, 0x3c4 bytes: the TIME TRIAL record books. Called from fn_1f858 at 0x2107e (the
 * drivers-by-class menu's choice 4). The metal menu is race_records_menu's with TRIALS rows; a metal
 * choice opens a second menu of five "<metal> TRACK n" rows plus LEAVE, and a track choice runs
 * trial_book(metal + v7ecb4, track, 0) between two v88cec = 0, then reloads CHECK.PCX. */
void trial_records_menu(void)
{
    char num[0x14];                           /* esp+0x00: itoa's buffer (0x1d590, 0x1d5e7) */
    char metal[0x10];                         /* esp+0x14: " BRONZE " etc., 9 bytes used */
    int saved = v88c80;                       /* esp+0x2c (0x1d349-0x1d358) */
    int sel = 1;                              /* esp+0x24 */
    int track, i;                             /* esp+0x28, esp+0x30 */

    for (;;) {
        v88bb0 = 0xd;                         /* 0x1d35c */
        v88b9e = 0;                           /* 0x1d369 */
        v88c80 = sel;                         /* 0x1d372 */
        if (v7ecb4 == 0) {                    /* 0x1d378: three stores here, the fourth at the join */
            strcpy(&mnames[0], "");
            strcpy(&mnames[0x3c], "");
            strcpy(&mnames[0x78], "");
            strcpy(&mnames[0xb4], "");
        } else {
            strcpy(&mnames[0], " BONUS");
            strcpy(&mnames[0x3c], " BONUS");
            strcpy(&mnames[0x78], " BONUS");
            strcpy(&mnames[0xb4], "");
        }
        strcat(&mnames[0], " BRONZE TRIALS   ");    /* 0x1d3cb */
        strcat(&mnames[0x3c], " SILVER TRIALS   "); /* 0x1d3fb */
        strcat(&mnames[0x78], " GOLD TRIALS     "); /* 0x1d42b */
        strcat(&mnames[0xb4], " LEAVE THIS MENU "); /* 0x1d45b */
        while (strlen(&mnames[0xb4]) < strlen(&mnames[0]))
            strcat(&mnames[0xb4], " ");             /* 0x1d48b-0x1d4db */
        sel = menu(4, 0, 0, 0);                      /* 0x1d4dd-0x1d4ee */
        if (v7ecc0 != 0)                            /* 0x1d4f2 */
            v88b9e = 1;
        if (v88b9e != 0)                            /* 0x1d504 */
            sel = 4;
        if (sel == 4)                               /* 0x1d516 -> 0x1d6f0 */
            break;
        switch (sel) {                              /* 0x1d521: the tree 2 / <2 / 3 */
        case 1: strcpy(metal, " BRONZE "); break;
        case 2: strcpy(metal, " SILVER "); break;
        case 3: strcpy(metal, " GOLD   "); break;
        }
        track = 1;                                  /* 0x1d55c */
        v88bda = 0;                                 /* 0x1d564 */
        for (;;) {
            v88c80 = track;                         /* 0x1d57e */
            for (i = 1; i <= 5; i++) {              /* 0x1d571, the test at 0x1d643 */
                strcpy(&mrows[i][0], metal);
                strcat(&mrows[i][0], "TRACK ");
                itoa(i, num, 10);                   /* 0x1d5e9 */
                strcat(&mrows[i][0], num);
                strcat(&mrows[i][0], "  ");
            }
            strcpy(&mnames[0x12c], " LEAVE THIS MENU ");  /* 0x1d64e-0x1d66b */
            track = menu(6, v88bda, 0, 0);          /* 0x1d671 */
            if (v7ecc0 != 0)                        /* 0x1d67b */
                v88b9e = 1;
            v88bda = 0;                             /* 0x1d68d */
            if (v88b9e != 0)                        /* 0x1d696 */
                track = 6;
            if (track == 6)                         /* 0x1d6a8 -> 0x1d35c */
                break;
            v88cec = 0;                             /* 0x1d6c3 */
            trial_book(sel + v7ecb4, track, 0);         /* 0x1d6b3-0x1d6ca */
            v88cec = 0;                             /* 0x1d6cf */
            v88bda = 1;                             /* 0x1d6dd */
            pcx_load("CHECK.PCX", 0); /* 0x1d6d6-0x1d6e6 */
        }
    }
    v88c80 = saved;                                 /* 0x1d6f0-0x1d6f4 */
}

/* ---- 0x1d704, 955 bytes: the record table. Draws LEAD.PCX, titles the box "<CLASS> 12 HP" or
 * "<CLASS> 6 HP" (v7ecb8 == 1) with msgbox, then the four records of group `row`, class `which`
 * (its 0x170-byte third of the tbl77758 row): name at y, face sprite, stamp, metal sprite
 * sp7c800[(row - v7ecb4 - 1) * 2] and the two-digit score; "POINTS" over the first score. Waits for
 * key88cd0 to drop after the fade. Callers: race_records_menu 0x1d321, scores_screen 0x1d12d. */
/* differs: near miss, 930 of the game's 955 bytes, 214 vs 215 instructions (the missing one is the
 * spill store); 7 diff blocks, two of them linker-filled displacements, and two causes (QUESTIONS 12):
 * 1. the game spills `which` to esp+0x94 after f278c4 (0x1d7b5: `mov [esp+0x94],ebx`, its frame is
 *    0x9c) and reads that slot for the msgbox row (0x1d843) and the three switch compares
 *    (`cmp word [esp+0x94],N`, 0x1d875-0x1d88f); ours keeps it in BX (frame 0x98, `cmp bx,N`). The
 *    23 bytes are those encodings. A K&R definition (short or int), a short local copy, an int
 *    copy, casts and moving the declarations do not spill it.
 * 2. (fixed iter 36: 0x1d73e is a short save of page_draw around `void pcx_blit(void)`; now 29 rows, the
 *    rest is cause 1 and the registers of the stamp row at 0x1d7f7.)
 * Cause 1 was probed again (iter 32, 11 spellings in two probe compiles, control 17 rows): a separate
 * local copy of `which` assigned at that statement and used for the msgbox row and the switch - short,
 * int, `volatile short`, cast, declared first vs last, assigned before/after wait_release, after the
 * strcats, used also by the strcats. Every one allocates the slot but puts the INCOMING parameter in it
 * at 0x1d70f (`mov [esp+0x94],eax`, no `mov ebx,eax`) and reloads EBX after the call - the mirror image
 * of the game's late store; 17-22 rows. Cause 2 is a documented dead end, so no spelling of cause 1
 * reaches MATCH. The prototype above is unprototyped (`void fn_1d704();`): a `short which` prototype makes both
 * callers sign-extend (race_records_menu 504 -> 472 bytes), so the game's callers saw none. */
void fn_1d704(short which, int row)
{
    char cls[3][40] = { "AMATEUR CLASS", "PRO CLASS", "CHAMP CLASS" };  /* 0x10184, 0x1d713 */
    char num[0x14];                           /* esp+0x78, itoa 0x1d9d8 */
    short i, y, ystamp, ypts;                 /* edi, esi, esp+0x98, esp+0x90: i first keeps the
                                               * game's register order (21 -> 17 diffs) */
    short sx;                                 /* esp+0x8c */

    pcx_load("LEAD.PCX", 2);    /* 0x1d71f-0x1d72b */
    v88b4a = 0;
    {
        short fs;
        p7d714 = p6a980;
        fs = page_draw;
        pcx_blit();            /* 0x1d739-0x1d758 */
        page_draw = fs;
    }
    page_to = 0x7d00;
    page_from = page_draw;
    page_copy();                             /* 0x1d766 */
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (page_draw == 0)                           /* 0x1d790 */
        page_draw = 0x3e80;
    else
        page_draw = 0;
    wait_release(0);                                /* 0x1d7b0 */
    page_from = 0x7d00;
    page_to = page_draw;
    page_copy();                             /* 0x1d7d1 */
    if (v7ecb8 == 1)                          /* 0x1d7d6 */
        strcat(cls[which - 1], " 12 HP");
    else
        strcat(cls[which - 1], " 6 HP");
    msgbox(cls[which - 1], 0x32);             /* 0x1d865 */
    switch (which) {                          /* 0x1d875: the tree 2 / <2 / 3 */
    case 1: v7d8a8 = tbl77758 + row * 0x450; break;
    case 2: v7d8a8 = tbl77758 + row * 0x450 + 0x170; break;
    case 3: v7d8a8 = tbl77758 + row * 0x450 + 0x2e0; break;
    }
    y = 0x53;
    i = 1;                                    /* 0x1d8b2, before the two slot stores */
    ystamp = 0x50;
    ypts = 0x3f;
    for (; i <= 4; i++) {                /* 0x1d8cd, the test at 0x1da43 */
        print_at(10, y, v7d8a8 + 0x1a, 0);    /* the name */
        sprptr = faces[*(short *)(v7d8a8 + 2)].img[0];
        spr_h = 0x26;
        spr_w = 0x1c;
        spr_x = 0x7a;
        spr_y = y - 0xc;
        spr_draw();                         /* 0x1d92f */
        spr_x += 0x30;
        print_at(spr_x, ystamp, v7d8a8 + 6, 0);   /* the stamp, 0x1d94c */
        sprptr = sp7c800[(row - (v7ecb4 + 1)) * 2];
        spr_h = 0x1c;
        spr_w = 0x17;
        spr_x += 8;
        spr_y = y - 7;
        spr_draw();                         /* 0x1d98e */
        if (i == 1) {
            sx = spr_x;
            print_at(sx, ypts, "POINTS", 0);  /* 0x1d9b6 */
            spr_x = sx;
        }
        spr_y = y - 7;
        itoa(*(short *)(v7d8a8 + 4), num, 10);    /* 0x1d9e5 */
        if (num[1] == 0) {                    /* one digit: "07"; num[0] is read first (0x1d9f1) */
            char c = num[0];
            num[2] = 0;
            num[0] = '0';
            num[1] = c;
        }
        print_at(spr_x + 0x22, (short)(spr_y + 7), num, 0);  /* 16-bit add + movsx, 0x1da07-0x1da22 */   /* 0x1da37 */
        ystamp += 0x1a;
        ypts += 0x1a;
        y += 0x1a;
        v7d8a8 += 0x2e;                       /* 0x1da3c */
    }
    page_show();                                 /* 0x1da4d */
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    wait_input(1);                             /* 0x1da77 */
    if (page_draw == 0)
        page_draw = 0x3e80;
    else
        page_draw = 0;
    wait_release(0);                                /* 0x1da9c */
    while (key88cd0 == 1)                     /* 0x1daa1 */
        ;
}

/* ---- 0x1dac0, 39 bytes: clears the two words of fn_1832c's 0x1839c loop for both 0x104-byte
 * cars (.f5a at 0x6847a, .face at 0x68426). No caller in the image (no call, no stored address): dead code kept by
 * the linker, like fn_1c88c. Defined after fn_1832c, so -oe cannot inline it there. */
void fn_1dac0(void)
{
    short i;                                  /* EDX: movsx eax,dx / cmp dx,1 */

    for (i = 0; i <= 1; i++) {
        cars[i].f5a = 0;                     /* +0x5a first, then the inc, then .face (0x183a7-0x183b1) */
        cars[i].face = 0;
    }
}

/* The game calls com_getc at 0x1dc7f (not inlined): com_getc_x. */
/* ---- 0x1dae8, 437 bytes: opens the serial link (caller 0x20943 in fn_1f858). Pauses the music with
 * a "MUSIC PAUSED" box, shows "INTERBYTE DELAY IS SET TO n" when comdelay is set (then
 * TS_SetTaskRate(comtask, 1000)), opens COM v7ec50 with the port/IRQ of its comports[] record and the
 * baud v7ebd4, waits 0x88 ticks (wait_or_key, inlined), drains the receiver and sets comopen. */
struct comport { int port; int irq; int f08; };   /* 12 bytes, 0x77240 (0x1db4b-0x1db61) */
extern struct comport comports[];
extern int v7ec50;                    /* the COM port number: index of comports, com_open's ECX arg (0x1dc2e: WCC's 4th parameter) */

void link_open(void)
{
    char num[0x2c];                   /* esp+0x00: itoa's buffer */
    char msg[0x28];                   /* esp+0x2c */
    int irq, port;                    /* port esp+0x54, irq esp+0x58 (this order gives that): dword loads and stores (0x1db52-0x1db69),
                                     * the movsx word at 0x1dc24 is com_open's short parameters */

    if (musicon) {                    /* 0x1daf0 */
        musicpaused = 1;
        music_pause();
        fn_1c068();
        msgbox("MUSIC PAUSED", 0);    /* 0x60048 */
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
    }
    port = comports[v7ec50].port;     /* 0x1db4b-0x1db69 */
    irq = comports[v7ec50].irq;
    itoa(v7ec50, num, 10);            /* 0x1db72, overwritten below when comdelay is set */
    if (comdelay) {                   /* 0x1db77 */
        itoa(comdelay, num, 10);
        strcpy(msg, "INTERBYTE DELAY IS SET TO ");
        strcat(msg, num);
        fn_1c068();
        msgbox(msg, 0);               /* 0x1dbdf */
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        TS_SetTaskRate(comtask, 1000);     /* 0x1dc1f */
    }
    com_open(port, irq, v7ebd4, v7ec50);   /* 0x1dc3c: WCC passes arg3 in EBX (baud = v7ebd4) and arg4 in ECX (0x1dc2e/0x1dc35) */
    wait_or_key(0x88);                /* 0x1dc41-0x1dc7d, inlined */
    while (com_getc_x() != -1)        /* 0x1dc7f */
        ;
    comopen = 1;                      /* 0x1dc8a */
}

/* ---- 0x1dca0, 527 bytes: waits for the modem's CONNECT (callers 0x1e0e5 in 0x1deb0, 0x1e22d in
 * 0x1e0f4). Returns 1 after a "CONNECTED" box, 0 with "ESCAPE CANCELLED WAIT" when the wait was
 * aborted. Like modem() above, the game has the fn_1c068/fn_1c0ac pair inline five times here
 * (0x1dca1, 0x1dd03, 0x1dd47, 0x1ddc9, 0x1de25) and calls it at 0x1de6b and 0x1dea6. */
int wait_connect(void)
{
    int ok;

    save_a = page_draw;                   /* fn_1c068, inline */
    save_b = page_from;
    save_c = page_to;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    msgbox("WAITING FOR CONNECT", 0);
    ok = com_waitfor("CONNECT");      /* 0x1dcf6 */
    if (ok) {
        page_from = 48000;                /* fn_1c0ac, inline */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        save_a = page_draw;               /* fn_1c068, inline */
        save_b = page_from;
        save_c = page_to;
        page_to = 48000;
        page_draw = page_shown;
        page_from = page_shown;
        page_copy();
        msgbox("CONNECTED", 0);       /* 0x1dd86 */
        ticks = 0;
        ticks2 = 0;
        do {
            if (aborted == 1 && connected) {
                sendfail = 1;
                break;
            }
        } while (ticks < 0x88);
        page_from = 48000;                /* fn_1c0ac, inline */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        return 1;
    }
    if (sendfail || !ok) {            /* 0x1de14-0x1de1f */
        page_from = 48000;                /* fn_1c0ac, inline */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        fn_1c068();
        msgbox("ESCAPE CANCELLED WAIT", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
    }
    return 0;
}

/* ---- 0x1deb0, 578 bytes: dials (caller 0x209f1 in fn_1f858). Shows "DIALING <phone>", sends
 * modemdial + the digits of phone (spaces dropped, one strcat per character), waits 0x110 ticks
 * before and after sending, then returns wait_connect(), or 0 with "CANCELLED" when a wait was
 * aborted. The fn_1c068/fn_1c0ac pair is written out on the main path and called on the cancel path
 * (as in modem and wait_connect). */
/* The game calls com_getc at 0x1dfc2 (not inlined): com_getc_x. */
extern char modemdial[];              /* 0x771bc, between modeminit and modemhangup (0x40 apart) */
extern char phone[];                  /* 0x7ebdc: the number to dial */

int dial(void)
{
    char cmd[0x2c];                   /* esp+0x00 */
    char msg[0x28];                   /* esp+0x2c */
    char c[2];                        /* esp+0x54 */
    unsigned i;                       /* EDX; jb against strlen (0x1df71) */

    i = 0;                            /* first: the game's xor edx,edx sits in the strcat (0x1decc) */
    strcpy(msg, "DIALING ");
    strcat(msg, phone);
    c[1] = 0;                         /* before the copy: the game's store is in its unit (0x1defb) */
    strcpy(cmd, modemdial);
    for (; i < strlen(phone); i++) {
        if (phone[i] != ' ') {
            c[0] = phone[i];
            strcat(cmd, c);
        }
    }
    save_a = page_draw;                   /* fn_1c068, inline */
    save_b = page_from;
    save_c = page_to;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    msgbox(msg, 0);                   /* 0x1dfbd */
    while (com_getc_x() != -1)
        ;
    wait_or_key(0x110);               /* 0x1dfcf-0x1e00b, inlined */
    sendstr(cmd);                     /* 0x1e00f */
    wait_or_key(0x110);
    page_from = 48000;                    /* fn_1c0ac, inline */
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
    if (sendfail) {                   /* 0x1e096 */
        fn_1c068();
        msgbox("CANCELLED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        return 0;
    }
    return wait_connect();            /* 0x1e0e5 */
}

/* ---- 0x1e0f4, 471 bytes: answers a call (caller 0x20a11 in fn_1f858). "WAITING FOR CALL....",
 * then com_waitfor("RING") until it succeeds or a wait is aborted (sendfail: "CANCELLED", 0);
 * on RING "ANSWERING CALL...", sends "ATA" and returns wait_connect(). */
int answer_call(void)
{
    save_a = page_draw;                   /* fn_1c068, inline */
    save_b = page_from;
    save_c = page_to;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
    msgbox("WAITING FOR CALL....", 0);
    while (1) {                       /* `for (;;)` + `if (x)` costs one instruction */
        if (com_waitfor("RING") != 0) {   /* 0x1e14a; the failed wait goes to 0x1e234, after the return */
        page_from = 48000;                    /* fn_1c0ac, inline */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        save_a = page_draw;                   /* fn_1c068, inline */
        save_b = page_from;
        save_c = page_to;
        page_to = 48000;
        page_draw = page_shown;
        page_from = page_shown;
        page_copy();
        msgbox("ANSWERING CALL...", 0);   /* 0x1e1da */
        sendstr("ATA");
        page_from = 48000;                    /* fn_1c0ac, inline */
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        return wait_connect();
        }
        if (sendfail != 0)            /* 0x1e234 */
            break;
    }
    page_from = 48000;                    /* fn_1c0ac, inline (0x1e241) */
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
    fn_1c068();
    msgbox("CANCELLED", 0);
    ticks = 0;
    ticks2 = 0;
    while (ticks <= 300)
        ;
    fn_1c0ac();
    return 0;
}

/* ---- 0x1e2cc, 21 bytes: clears v684e0 and v685e4 (the pair classes_menu and fn_1832c clear inline).
 * No caller in the image: dead code kept by the linker. */
void fn_1e2cc(void)
{
    v684e0 = 0;
    v685e4 = 0;
}

/* differs: near miss, 1015 of the game's 1013 bytes, 221 vs 220 instructions, three causes:
 * 1. the loop top (0x1e4d2): the game loads v7ec60 and stores netvoices/v7ec90/net88c22/myplayer/
 *    w88bce before `nlaps = 10; v88b84 = 0x59`; ours emits those two constant stores first. All 28
 *    source positions of the pair, in both orders, give the same code (.work/p14/gdemo.py).
 * 2./3. (fixed iter 36: pcx_blit is pushal..popal with no arguments; `void pcx_blit(void)` and a
 *    short save of page_draw give the game's `mov dx,[page_draw]` 0x1e472 and drop the `mov edx,ebx`.)
 *    14 rows left, all cause 1. */
/* ---- 0x1e2e4, 1013 bytes: the demo races. mode 3 when the menus sit idle (v7ed0c >= 500, callers
 * 0x1faaf/0x1fb04), mode 1 from a menu entry (0x20f48). The mode picks the picture set ("OR"/"INT",
 * rec7c208[1..n] = "<set><i>.PCX" for n = 11/8/1) and the starting group/class (v7ec5c, v7ec58).
 * Then, with v7ee58/v7ee5a saved and forced to 2/1, it runs one single-player race (0x2fd8c with
 * countplays = 1) per pass, rotating the driver v7ec60 through 0-7, until a race sets v7ec68 or
 * v7ec58 passes 5. */
extern short w88c98, w88c9a, w88bce, w88c08;
extern int v7ec5c, v7ec58, v7ec60, v7ec90, v7ec68;
void race_run(void);

void demo_mode(int mode)              /* arg1 = eax, kept in esp+0x28 */
{
    char set[0x14];                   /* esp+0x00 */
    char num[0x14];                   /* esp+0x14 */
    int i;                            /* esp+0x2c */
    int s58, s5a;                     /* ESI, EDI: movsx loads (0x1e461) */
    short k;

    w88c98 = 0;
    v7edb0 = 0;
    switch (mode) {                   /* 0x1e304: the tree 2 / <2 / 3 */
    case 1:
        v7ec5c = 2;
        strcpy(set, "OR");
        v7ec58 = 2;
        w88c9a = 0xb;
        break;
    case 2:
        v7ec5c = 1;
        v7ec58 = 1;
        strcpy(set, "INT");
        w88c9a = 8;
        break;
    case 3:
        v7ec5c = 1;
        strcpy(set, "OR");
        v7ec58 = 2;
        w88c9a = 1;
        break;
    }
    v88c8e = 1;
    for (i = 1; i <= w88c9a; i++) {   /* 0x1e397-0x1e44a, rec7c208[i] in EBP */
        itoa(i, num, 10);
        strcpy(rec7c208[i].f00, set);
        strcat(rec7c208[i].f00, num);
        strcat(rec7c208[i].f00, ".PCX");
    }
    v7d998 = (mode == 3) + 2;         /* 0x1e450-0x1e485 */
    v88b4a = 0;
    s58 = v7ee58;
    s5a = v7ee5a;
    v7ee58 = 2;
    v7ee5a = 1;
    {
        short fs;
        p7d714 = p6a978;
        fs = page_draw;
        pcx_blit();    /* 0x1e49d */
        page_draw = fs;
    }
    v7ecb4 = 0;
    detail_lvl = v7ee58;
    v7ea58 = v7ee5a;
    for (;;) {
        netvoices = 0;                /* 0x1e4d2 */
        v7ec90 = 0;
        net88c22 = 0;
        myplayer = v7ec60;
        w88bce = 0;
        nlaps = 10;
        v88b84 = 0x59;
        twoplayer = 0;
        splitscreen = 0;
        v88c58 = 0;
        v88b9e = 0;
        v7ec68 = 0;
        v88bb2 = 1;
        v684e0 = 0;
        v685e4 = 0;
        players[0].f1e = 0;           /* 0x1e545-0x1e582: eight stores, no loop */
        players[1].f1e = 0;
        players[2].f1e = 0;
        players[3].f1e = 0;
        players[4].f1e = 0;
        players[5].f1e = 0;
        players[6].f1e = 0;
        players[7].f1e = 0;
        v88bb6 = v7ec5c;
        v88b88 = v7ec58;
        strcpy(cls_name, "");
        for (k = 0; k <= 1; k++) {
            cars[k].f5a = 0;
            cars[k].face = 0;
        }
        fn_1be3c();                   /* 0x1e5b9 */
        w88c08 = 0;
        fn_17928();
        countplays = 1;
        race_run();                     /* 0x1e5d0: the race */
        countplays = 0;
        voices_reset();
        if (++v7ec60 > 7)             /* 0x1e5e0 */
            v7ec60 = 0;
        palptr = pal7d850;
        getpalette();
        fade_out(pal7d850);
        pcx_load("CHECK.PCX", 0);   /* 0x1e615 */
        if (v7ec68 != 0)              /* 0x1e61a */
            break;
        if (++v7ec58 > 5)             /* 0x1e626 */
            break;
        v88b4a = 0;
        page_draw = 0;
        p7d714 = p6a978;
        pcx_blit();                     /* 0x1e64d */
        page_to = 0x3e80;
        page_draw = 0;
        page_from = 0;
        page_copy();
        page_show();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        fade_in((unsigned char *)v7d85c);               /* 0x1e695 */
    }
    v7d998 = 0;                       /* 0x1e69f */
    v88bda = 1;
    v7ee58 = s58;
    v7ee5a = s5a;
    detail_lvl = v7ee58;                  /* re-read as the setup does: movsx eax,di (0x1e6c0) */
    v7ea58 = v7ee5a;
}

/* ---- 0x1e6dc, 132 bytes: keeps one looping sample going (callers 0x1eab7, 0x1ecb5 in 0x1e760).
 * With no voice (v7eaf8 == -1) it plays p7abc0 at fxvol, priority 1. When that voice ends it sets
 * the pause flag v7eb08 and restarts ticks2, and the next start waits until ticks2 >= 0x13. */
extern int v7eb08, v7eaf8;
extern char *p7abc0;

void fn_1e6dc(void)
{
    int play = 0;                     /* ECX */

    if (v7eb08 != 0) {                /* 0x1e6e1 */
        if (ticks2 >= 0x13)
            v7eb08 = play;
        return;
    }
    if (v7eaf8 != -1) {               /* 0x1e6fd; `play = 1` is the cold arm (0x1e730) */
        if (!FX_SoundActive(v7eaf8)) {
            v7eb08 = 1;
            v7eaf8 = -1;
            ticks2 = play;
        }
    } else
        play = 1;
    if (play)                         /* 0x1e735 */
        v7eaf8 = FX_PlayVOC(p7abc0, 0, fxvol, fxvol, fxvol, 1, 0);   /* fxvol 0x77428, not sfxvol */
}

/* differs: near miss, 48 rows (iter 38: an explicit row pointer `r`, declared after vol and set in the
 * for header, 64 -> 50 -> 48 and blink takes the game's 0x110; `barx = (spr_h - 2) + spr_x` loads
 * spr_h first, 3 rows). Left: 30 rows of scalar-slot permutation (dead end), the game's pointer
 * init inside the n >= 1 guard (`mov esi,esp; add esi,0x28`; ours hoists `lea esi,[esp+0x28]`
 * above it), the barx add direction (`add edx,eax` vs ours `add eax,edx`, every spelling) and:
 * Older note (1921 of the game's 1922 bytes, 402 vs 403 instructions), two causes:
 * 1. the frame wait (0x1ecba): the game loads both operands, `mov eax,[v7e724]; mov edx,[ticks];
 *    sub edx,eax`, ours `mov ebx,[ticks]; sub ebx,[v7e724]`. cars_menu's identical-looking
 *    `ticks - v7ea70` loop (0x1a9f7) matches, but there no call sits in the loop. Tried: volatile
 *    v7e724 (6 blocks), `for (;;)` + break, `0xe > ...`, `ticks < v7e724 + 0xe` and a leading
 *    `while` (all worse or equal).
 * 2. (fixed iter 36: 0x1e816 is a short save of page_draw around `void pcx_blit(void)`; the slot
 *    permutation re-shuffled with it, 53 -> 64 rows; `int fs` at function level 57 but `movzx`.)
 * The levers that did matter: `k = kind[i]` read first and compared as a register, `spr_h = vol * 4`
 * before the other three sprite stores, `ys[i]` before `spr_x = 10`, `vol < 0` (not < 1) and
 * `spr_x + (spr_h - 2)`. */
/* ---- 0x1e760, 1922 bytes: the SOUND VOLUMES screen (caller 0x21198 in fn_1f858). Rows are indexed
 * from 1 like mrows: MUSIC when musicready, SOUND FX and ENGINE FX when sfxsoft, kind[] 1/2/3.
 * Each row's volume is a bar of (vol / 5) clamped to 1..0x33, times 4 pixels (box_draw); a blinking
 * cursor (gn7eae4) marks the selected row while fn_1e6dc keeps the test sample looping. Left and
 * right change the volume by 5 within 5..255 and apply it at once; up and down move the row; ESC
 * (key88cd0) saves the config. */
extern int v7e724;                    /* 0x7e724: ticks at the start of a frame (0x1eadf) */

void volume_menu(void)
{
    char rows[4][0x28];               /* esp+0x00, rows[1..3] */
    char buf[0x50];                   /* esp+0xa0: the centred help lines */
    int ys[4];                        /* esp+0xf0: each row's y */
    int kind[4];                      /* esp+0x100: 1 music, 2 sound fx, 3 engine */
    int blink;                        /* esp+0x110 */
    int textx;                        /* esp+0x114: x after the label text */
    int barx;                         /* esp+0x118 */
    int vol;                          /* esp+0x128 */
    char *r;
    int n, sel, i, changed, k;        /* EBP, ESI, ECX, EDX, EAX */

    if (!musicready && !sfxsoft) {    /* 0x1e76c */
        fn_1c068();
        msgbox("BOTH SOUND DEVICES SET TO NONE!", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        return;
    }
    netvoices = 0;                    /* 0x1e7c5 */
    v7ec90 = 0;
    twoplayer = 0;
    splitscreen = 0;
    blink = 0;
    v88bda = 0;
    v88b9e = 0;
    v88b4a = 0;
    {
        short fs;
        p7d714 = p6a978;
        fs = page_draw;
        pcx_blit();    /* 0x1e824 */
        page_draw = fs;
    }
    n = 0;
    if (musicready) {
        n = 1;
        strcpy(rows[n], "MUSIC     -");
        kind[n] = 1;
    }
    if (sfxsoft) {                    /* 0x1e851 */
        n++;
        strcpy(rows[n], "SOUND FX  -");
        kind[n] = 2;
        n++;
        strcpy(rows[n], "ENGINE FX -");
        kind[n] = 3;
    }
    strcpy(buf, "UP AND DOWN TO SELECT");
    spr_y = 0xb0;
    spr_x = (0x140 - (int)strlen(buf) * 8) / 2;
    print_at(spr_x, spr_y, buf, 0);   /* 0x1e903 */
    strcpy(buf, "LEFT AND RIGHT TO ADJUST. ESC TO QUIT");
    spr_y += 10;
    spr_x = (0x140 - (int)strlen(buf) * 8) / 2;
    print_at(spr_x, spr_y, buf, 0);
    strcpy(buf, "SOUND VOLUMES");
    spr_y = 0x46;
    spr_x = (0x140 - (int)strlen(buf) * 8) / 2;
    print_at(spr_x, spr_y, buf, 0);   /* 0x1e9b6 */
    spr_y += 0x14;
    for (i = 1, r = rows[1]; i <= n; i++, r += 0x28) {        /* 0x1e9c3-0x1ea63 */
        ys[i] = spr_y;
        spr_x = 10;
        textx = spr_x + strlen(r) * 8;
        print_at(spr_x, spr_y, r, 0);
        spr_y += 0x14;
    }
    page_to = 0xbb80;                   /* 0x1ea65 */
    page_from = page_draw;
    page_copy();
    sel = 1;
    ticks2 = 0;
    v7eaf8 = -1;
    v7eb08 = 0;
    page_from = page_to;
    if (sfxsoft) {                    /* 0x1eaae */
        fn_1e6dc();
        cars[0].sndhandle = -1;
        cars[1].sndhandle = -1;
        engine_start();
    }
    for (;;) {
        v7e724 = ticks;               /* 0x1eada */
        page_to = page_draw;
        page_copy();
        for (i = 1; i <= n; i++) {    /* 0x1eafa-0x1ebeb */
            k = kind[i];
            spr_x = textx;
            spr_y = ys[i] + 2;
            if (k == 1)
                vol = musicvol;
            else if (k == 2)
                vol = fxvol;
            else
                vol = enginevol;
            vol = vol / 5;
            if (vol < 0)              /* test eax,eax / jge (0x1eb77): < 0, not < 1 */
                vol = 1;
            if (vol > 0x33)
                vol = 0x33;
            spr_h = vol * 4;
            spr_w = 5;
            ink = 0x2c;
            border = 0x26;
            box_draw();                 /* 0x1ebc2: the bar */
            if (i == sel)
                barx = (spr_h - 2) + spr_x;
        }
        spr_x = barx;                 /* 0x1ebf1 */
        spr_y = ys[sel] - 8;
        if (blink) {
            blink = 0;
            spr_x = barx - 2;
            spr_y += 2;
        } else
            blink = 1;
        spr_h = 0xc;
        spr_w = 0xd;
        sprptr = gn7eae4;
        spr_draw();                 /* 0x1ec64 */
        page_show();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        if (page_draw == 0)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        do {
            if (sfxsoft)
                fn_1e6dc();
        } while (ticks - v7e724 < 0xe);   /* 0x1ecac-0x1ecca */
        ticks2++;
        if (key88cd0 == 1)            /* 0x1ecd2 */
            break;
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();                     /* 0x1ecf0 */
        changed = 0;
        switch (kind[sel]) {          /* 0x1ecfe */
        case 1: vol = musicvol; break;
        case 2: vol = fxvol; break;
        case 3: vol = enginevol; break;
        }
        if (key88cd5 || v7b6d9) {     /* left */
            vol -= 5;
            blink = 0;
            if (vol < 5)
                vol = 5;
            changed = 1;
        }
        if (key88cdd || v7b6db) {     /* right */
            blink = 0;
            vol += 5;
            changed = 1;
            if (vol > 0xff)
                vol = 0xff;
        }
        if (changed) {                /* 0x1edac */
            switch (kind[sel]) {
            case 1:
                musicvol = vol;
                MUSIC_SetVolume(vol);
                break;
            case 2:
                fxvol = vol;
                FX_SetVolume(vol);
                sfxvol = fxvol;
                if (soundon) {
                    FX_StopSound(cars[0].sndhandle);
                    cars[0].sndhandle = -1;
                }
                engine_start();
                break;
            case 3:
                enginevol = vol;
                if (soundon) {
                    FX_StopSound(cars[0].sndhandle);
                    cars[0].sndhandle = -1;
                }
                engine_start();
                break;
            }
        }
        if (key88cd6 || key_up) {   /* up, 0x1ee50 */
            blink = 0;
            if (sel > 1)
                sel--;
        }
        if (key88cd3 || key_down) {   /* down, 0x1ee73 */
            blink = 0;
            if (sel < n)
                sel++;
        }
    }
    while (key88cd0 == 1)             /* 0x1eea2 */
        ;
    if (sfxsoft)
        FX_StopAllSounds();
    save_config(1);                   /* 0x1eebe */
    v88bda = 0;
    v88b9e = 0;
}

/* ---- 0x1eee4, 201 bytes: prints n centred credit lines from line `first` (1-based) at y 0x4c, 10
 * apart, and records each line's box (x, y, right edge; 0 for an empty line) in cboxes[1..n]
 * (callers 0x1f02b, 0x1f41c in 0x1efb0). `first` itself is the line counter: the game keeps the
 * incremented parameter in esp+0 though nothing reads it, which a separate `i` does not give. */
struct cbox { short x, y, x2; };     /* 6 bytes */
extern char credits[][0x2c];          /* 0x632f0: "PROGRAMMING AND DESIGN", ... */
extern struct cbox cboxes[];          /* 0x7b198, used from [1] */

void credits_page(int n, int first)
{
    int k, w;
    int x;                            /* EAX, kept for print_at's cwde (0x1ef60) */

    spr_y = 0x4c;
    for (k = 1; k <= n; k++) {
        w = strlen(credits[first - 1]) * 8;
        x = (0x140 - w) / 2;
        spr_x = x;
        cboxes[k].x = x;
        cboxes[k].x2 = w - 1 + x;
        cboxes[k].y = spr_y;
        print_at(spr_x, spr_y, credits[first - 1], 0);
        if (strlen(credits[first - 1]) == 0)
            cboxes[k].x2 = 0;
        first++;
        spr_y += 10;
    }
}

/* differs: near miss, 1164 bytes like the game since iter 36, 32 rows, two causes:
 * 1. slots: the game has dir at esp+0x08 and y at esp+0x10, ours the other way round (QUESTIONS 9/10
 *    class); 48 declaration orders tried, `dir, page, first, y` is the closest (52 -> 47 scored).
 *    (The `mov edx,1` at 0x1f01f and the reload at 0x1f414 went with `void pcx_blit(void)`, iter 36.)
 * 2. scheduling around page_copy: the game computes `imul edi,esi,0x98` (face) into the dead save
 *    register before the call and stores page_from last (0x1f0d0-0x1f10e). Tried iter 36: all 6 orders of
 *    the page_draw/page_to/page_from stores (no change), `char **img = faces[face].img` before the call (44),
 *    `struct face *fp` (44), save reused as `face * 0x98` (75).
 * Levers that did matter: `int save` (movzx), `if (dir != 0)` arms in both places, and `++face`
 * repeated in each direction arm. */
/* ---- 0x1efb0, 1164 bytes: the credits (caller 0x21099 in fn_1f858). Up to five pages of 12 lines
 * (credits_page); on each, a driver's face (faces[face].img[frame], frames 0-7) walks along the
 * printed lines - right along one line, left along the next, skipping empty lines and switching to
 * the next driver at each line - then runs in place for 0x88 ticks2 or until a key or button.
 * ESC (key88cd0) leaves at any point. */
void credits_screen(void)
{
    int save;                         /* EDI: movzx loads (0x1efd2) */
    int first, page, dir, y;          /* esp+0x08, esp+0x00, esp+0x0c, esp+0x10: this order is the closest */
    int k, x, face, frame;            /* EDX, EBX, ESI, ECX */

    page = 1;
    first = 1;
    save = page_draw;
    pcx_load("CHECK.PCX", 2);   /* 0x1efd9 */
    v88b4a = 0;
    page_draw = 0x7d00;
    p7d714 = p6a980;
    pcx_blit();
    page_draw = 0xbb80;
    page_from = 0x7d00;
    page_to = 0xbb80;
    page_copy();
    face = 0;
    credits_page(12, first);          /* 0x1f02b */
    page_draw = save;
    for (;;) {
        k = 1;                        /* 0x1f037 */
        x = cboxes[1].x;              /* 0x1f03c: .x (0x7b19e), not .x2 */
        frame = 0;
        dir = 1;
        for (;;) {
            y = cboxes[k].y - 10;     /* 0x1f049 */
            ticks = 0;
            save = page_draw;
            page_draw = 0xbb80;
            if (dir != 0)
                spr_x = x;
            else
                spr_x = x + 0x26;
            spr_h = 8;
            spr_y = cboxes[k].y;
            spr_w = 8;
            sprptr = p6a97c;
            spr_grab();              /* 0x1f0bd */
            page_draw = 0x7d00;
            spr_draw_opaque();
            page_draw = save;
            page_to = save;
            page_from = 0x7d00;
            page_copy();
            spr_h = 0x26;
            spr_w = 0x1c;
            spr_y = y;
            sprptr = faces[face].img[frame];
            spr_x = x;
            spr_draw();             /* 0x1f121 */
            page_show();
            if (key88ccd) {
                screenshot();
                while (key88ccd)
                    ;
            }
            if (page_draw == 0)
                page_draw = 0x3e80;
            else
                page_draw = 0;
            while (ticks <= 4)
                ;
            if (key88cd0 == 1)        /* 0x1f172 */
                return;
            if (++frame > 7)
                frame = 0;
            if (dir != 0) {           /* 0x1f197 */
                x += 8;
                if (x <= cboxes[k].x2)
                    continue;
                k += 1;
                if (k > 12)
                    break;
                if (cboxes[k].x2 == 0)
                    k++;
                if (k >= 12 && cboxes[k].x2 == 0)
                    break;
                dir = 0;
                x = cboxes[k].x2 - 0x26;
                if (++face > 7)
                    face = 0;
            } else {                  /* 0x1f205 */
                x -= 8;
                if (x > cboxes[k].x - 0x2e)
                    continue;
                k++;
                if (k > 12)
                    break;
                if (cboxes[k].x2 == 0)
                    k++;
                if (k >= 12 && cboxes[k].x2 == 0)
                    break;
                dir = 1;
                x = cboxes[k].x;
                if (++face > 7)
                    face = 0;
            }
        }
        ticks2 = 0;                   /* 0x1f264 */
        if (dir != 0)
            x += 0x10;
        else
            x -= 0x10;
        for (;;) {
            ticks = 0;                /* 0x1f288 */
            page_from = 0xbb80;
            page_to = page_draw;
            page_copy();
            spr_h = 0x26;
            spr_w = 0x1c;
            spr_y = y;
            sprptr = faces[face].img[frame];
            spr_x = x;
            spr_draw();
            page_show();
            if (key88ccd) {
                screenshot();
                while (key88ccd)
                    ;
            }
            while (ticks <= 12)
                ;
            ticks2++;
            if (page_draw == 0)
                page_draw = 0x3e80;
            else
                page_draw = 0;
            if (ticks2 > 0x88)        /* 0x1f331 */
                break;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            v7d8a4 = v7d8ac;
            joy88b46 = 0;
            buttons_poll();
            if (key_select || joy88b40 || joy88b42 || joy88b44 || joy88b46)
                break;
            if (key88cd0 == 1)
                return;
            if (++frame > 7)
                frame = 0;
        }
        v88b4a = 0;                   /* 0x1f3c3 */
        first += 12;
        save = page_draw;
        page_draw = 0x7d00;
        p7d714 = p6a980;
        pcx_blit();
        page_draw = 0xbb80;
        page_from = 0x7d00;
        page_to = 0xbb80;
        page_copy();
        page++;
        credits_page(12, first);      /* 0x1f41c */
        page_draw = save;
        if (page > 5)                 /* 0x1f428 */
            return;
    }
}

/* ---- 0x1f43c, 803 bytes: the link game menu (caller 0x1f9d9 in fn_1f858). Mode: RACE HEAD TO HEAD
 * (v7ed9c 1), RACE WITH ALL CARS (2), SHOOT OUT GAME (net88c22 3) or TERMINATE LINK (net88c22 4);
 * then SPLIT SCREEN or FULL SCREEN (v88bd0). Each side sends its detail level (v774de split, v7ee58
 * full) with net_key, keeps the lower detail of the two (the higher number) in w88cb8 and says which. */
extern int v7ed9c;
extern short w88cb8;

void link_menu(void)
{
    short sel;                        /* EDX after the second menu */

    do {
        strcpy(&mnames[0], " RACE HEAD TO HEAD  ");    /* 0x1f441 */
        v88b9e = 0;
        strcpy(&mnames[0x3c], " RACE WITH ALL CARS ");
        v88bb0 = 0x10;
        v7ed20 = 0x198;
        v7ed1c = 0x198;
        strcpy(&mnames[0x78], " SHOOT OUT GAME     ");
        strcpy(&mnames[0xb4], " TERMINATE LINK     ");
        v88c80 = 1;
        v88c80 = menu(4, v88bda, 0, 0);           /* 0x1f4c8 */
        if (v88bda)
            v88bda = 0;
        if (net88c14)
            return;
        if (v88c80 == 4) {
            net88c22 = 4;
            return;
        }
        if (v88c80 == 1) {
            v7ed9c = 1;
            net88c22 = 2;
        }
        if (v88c80 == 2) {
            v7ed9c = 2;
            net88c22 = 2;
        }
        if (v88c80 == 3)
            net88c22 = 3;
        v88bd0 = 1;
        strcpy(&mnames[0], " SPLIT SCREEN MODE ");     /* 0x1f559 */
        v88bb0 = 0xd;
        strcpy(&mnames[0x3c], " FULL SCREEN MODE  ");
        v88c80 = 1;
        sel = menu(2, 0, 0, 0);                   /* 0x1f5a1 */
        v88bd0 = sel;
        if (net88c14)
            return;
    } while (v7ecc0 != 0);                        /* 0x1f5bc */
    if (sel == 2)
        v88bd0 = 0;
    if (v88bd0 != 0)
        w88cb8 = v774de;
    else
        w88cb8 = v7ee58;
    net88c14 = net_key((unsigned char)w88cb8, 0, 0);   /* 0x1f601 */
    {
        int start = ticks;
        while (ticks - start < 0x46)
            ;
    }
    if (net88c14)
        return;
    if (v88cef > w88cb8)                          /* 0x1f62c: the other side's level */
        w88cb8 = v88cef;
    switch (w88cb8) {                             /* 0x1f643: the tree 2 / <2 / 3 */
    case 1:
        fn_1c068();
        msgbox("HIGH DETAIL SELECTED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        break;
    case 2:
        fn_1c068();
        msgbox("MEDIUM DETAIL SELECTED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        break;
    case 3:
        fn_1c068();
        msgbox("LOW DETAIL SELECTED", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        fn_1c0ac();
        break;
    }
    cars[0].fd0 = 0;                              /* 0x1f731 */
    cars[1].fd0 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
}

/* ---- 0x1f760, 196 bytes: fills the main menu's nine rows (callers 0x1f963, 0x1f9a1, 0x1fa7d in
 * fn_1f858). " COMM#BAT~ PLAY" is the game's own spelling of the row text (0x61118). */
void main_menu_names(void)
{
    strcpy(&mnames[0], " SINGLE PLAYER RACING ");
    strcpy(&mnames[0x3c], " TWO PLAYER RACE      ");
    strcpy(&mnames[0x78], " TWO PLAYER SHOOT OUT ");
    strcpy(&mnames[0xb4], " WACKY DUCK SHOOT     ");
    strcpy(&mnames[0xf0], " COMM#BAT~ PLAY       ");
    strcpy(&mnames[0x12c], " GAME OPTIONS         ");
    strcpy(&mnames[0x168], " WACKY WHEELS INFO    ");
    strcpy(&mnames[0x1a4], " ORDERING INFO        ");
    v88bb0 = 1;                       /* 0x1f812, inside the last copy's unit */
    strcpy(&mnames[0x1e0], " LEAVE GAME           ");
}

/* ---- 0x1f858-0x2130c, 0x1ab5 bytes: the main menu dispatcher (no callers in the code: entered from
 * the game's main loop setup). Loops forever around menu(9): 1 one player (cfgs[] restore, cars, the
 * race loop), 3 two player, 4 the shoot-out (voices on), 5 the link (modem/serial, phonebook, SYNC and
 * version check), 6 options (detail, volumes, race-screen toggles), 7 info (ordering, instructions,
 * famous drivers, credits), 8 the demo, 9 return (the jump table sends 9 to engines_menu's identical
 * epilogue at 0x16978: GOTCHAS cross-function epilogue merge). v88bd4 != 0 shows the menu once with
 * the palette reloaded and item 1 forced.
 * differs (near-miss, 1265 vs 1265 insns, 58 diff blocks; variants.py 360 instrdiffs, most of them
 * one shifted alignment): (a) the game keeps the item count 9 in a short local in EDX (`mov edx,9`
 * before ents_init, `movsx eax,dx` at the three menu(9) calls) and saves page_draw in the same DX around
 * every pcx_blit fade; our build folds the 9 into `mov eax,9` and saves page_draw in BX (8 fade sites). One
 * shared local m, separate locals, and six declaration orders (M00-M05) all fold it; (b) case 9's
 * `return` is engines_menu's epilogue in the game (jump table entry 0x16978), our build emits its own
 * (also with `goto leave` and with main_menu moved to its address position in the file);
 * (c) `dec esi; cmp esi,8` switch vs our `lea eax,[esi-1]`; (d) the CHECK.PCX block of the race
 * loop is laid out before the `v88b88++` block in the game, after it in ours (a goto/if-else shape
 * makes 9.5b merge it into the first CHECK.PCX block instead). Case 7 before case 6 in the source
 * gives the game's layout (360 vs 596 instrdiffs). Iter 40: `net_key(v7eca0, 0, 0)` without the
 * `& 0xff` (the unsigned char parameter truncates; the game's one `movzx eax, byte [v7eca0]` 0x20c3c)
 * 92 -> 79 rows, tools/wc/fndiff.py; the case 3/4 `v7eca8 = v7ee5a; v7ee5a = 2;` pair through an int
 * temp (t) is worse: 79 -> 99 (9.5b sinks the load+stores past the five constant stores either way).
 * nmrefs iter 40: ours carries 765 of the game's 773 references; the game has one more each to
 * fade_out_screen, p7d718, page_draw and v88bda, all inside blocks one side only (4 of them) - a block-order lead
 * for (d), not a missing statement. */
struct cfgp {                       /* 0x774e4, 0x74 bytes, [0] one-player and [1] split-screen saved game */
    int on;
    int myplayer, f08, laps, f10, f14, f18;
    int f1c[8];                     /* -> players[i].f1e */
    int f3c[8];                     /* -> v70720[i] */
    int f5c, f60, f64, f68, f6c, f70;
};
struct pbook { int f00; char name[0x16]; char number[0x16]; };   /* 0x77284, 0x30 bytes, [1..6] */
extern struct cfgp cfgs[];
#pragma aux cfgs "_ctl774e4"
extern struct pbook pbook[];
extern int cfg77174, cfg77178, v7ebb4, v7ebd0, v7ebd8;
extern short v88bd4, v88c1e, optmap, optclock, optspeed;
extern char *v7d708;
extern char str7c908[], str7c944[];
/* fade_out_screen 0x1bf80 is defined above; called by its own name -oe inlines it here, where the
 * game calls 0x1bf80 (0x1ffa9, 0x200a9), so main_menu calls it through an alias. */
void fade_out_screen_call(void);
#pragma aux fade_out_screen_call "fade_out_screen_"
void digger_load(void);
void faces_reset(void);
void duckfame_write(void);
short net_sync(void);

#define FADE() { m = page_draw; page_draw = 0x7d00; p7d714 = p6a978; pcx_blit(); page_draw = m; }
#define WAIT300() { ticks = 0; ticks2 = 0; while (ticks <= 0x12c) ; }
#define PALSAVE() { save_a = page_draw; save_b = page_from; save_c = page_to; page_to = 48000; page_draw = page_shown; page_from = page_shown; page_copy(); }
#define PALREST() { page_from = 48000; page_draw = page_shown; page_to = page_shown; page_copy(); page_from = save_b; page_to = save_c; page_draw = save_a; }

void main_menu(void)
{
    short i, j, k, s7, s6, m;
    int sel, a, t;

    v88bda = 0;
    net88c22 = 0;
    for (;;) {
        v7edb0 = 0;
        v7ec60 = 0;
        v7ed0c = 0;
        v7d998 = 0;
        v7ecb4 = 0;
        anidope = 0;
        v88ca0 = optmap;
        w88c98 = 0;
        if (cursong != menusong)
            music_start(menusong, 1);
        ticks = 0;
        ticks2 = 0;
        v88b9e = 0;
        netvoices = 0;
        v7ec90 = 0;
        twoplayer = 0;
        splitscreen = 0;
        m = 9;
        ents_init();
        v88b88 = 1;
        main_menu_names();
        v88bb0 = 1;
        if (v88bd4 == 0) {
            if (netgame) {
                for (;;) {
                    if (net88c14 != 0 || net88c22 == 4) {
                        main_menu_names();
                        sel = menu(m, v88bda, 2, 0);
                        v7ed0c = 0;
                        net88c14 = 0;
                        break;
                    }
                    link_menu();
                    v7ed0c = 0;
                    if (v88bda)
                        v88bda = 0;
                    if (net88c14 == 0 && net88c22 != 4) {
                        sel = net88c22;
                        break;
                    }
                }
            } else {
                main_menu_names();
                sel = menu(m, v88bda, 0, 0);
                if (v7ed0c >= 500) {
                    demo_mode(3);
                    continue;
                }
            }
            if (v88bda)
                v88bda = 0;
        } else {
            v88bd4 = 0;
            palptr = pal7d850;
            getpalette();
            fade_out(pal7d850);
            sel = menu(m, 1, 0, 0);
            if (v7ed0c >= 500) {
                demo_mode(3);
                continue;
            }
        }
        splitscreen = 0;
        v88bb8 = sel;
        if (netgame == 0)
            v7ed9c = 1;
        if (sel == 2) {
            sel = 1;
            splitscreen = v7ed9c;
        }
        switch (sel) {
        case 1:
            net88c22 = 0;
            w88bce = 0;
            twoplayer = 0;
            netvoices = 0;
            v7ec90 = 0;
            v88c58 = 0;
            v88bb6 = 1;
            v88b88 = 1;
            v88b9e = 0;
            v88b4a = 0;
            FADE();
            w88c98 = 0;
            if (netgame == 0) {
                if (splitscreen) {
                    if (cfgs[1].on)
                        w88c98 = 2;
                } else if (cfgs[0].on)
                    w88c98 = 1;
            }
            if (w88c98) {
                v7ecb4 = 0;
                v88c80 = 1;
                strcpy(&mnames[0], " RESTORE SAVED GAME ");
                v88b9e = 0;
                strcpy(&mnames[0x3c], " START NEW GAME     ");
                v88bb0 = 0xd;
                v88c80 = menu(2, 0, 0, 0);
                v88b4a = 0;
                FADE();
                if (v88c80 == 2) {
                    cfgs[w88c98 - 1].on = 0;
                    w88c98 = 0;
                }
            }
            v88b7e = 0;
            if (w88c98 == 0) {
                if (splitscreen == 1 && netgame == 0) {
                    v88c80 = 1;
                    strcpy(&mnames[0], " RACE HEAD TO HEAD  ");
                    v88b9e = 0;
                    strcpy(&mnames[0x3c], " RACE WITH ALL CARS ");
                    v88bb0 = 0xd;
                    v88c80 = menu(2, 0, 0, 0);
                    v88b4a = 0;
                    FADE();
                    if (v7ecc0)
                        v88b9e = 1;
                    if (v88b9e)
                        continue;
                    splitscreen = v88c80;
                }
                fn_1a2f8();
                cars_menu();
            } else {
                v88c58 = 0;
                v88b9e = 0;
                if (splitscreen) {
                    v88b9e = 1;
                    splitscreen = ctl775cc;
                }
                if (v88b9e == 0) {
                    v7ee5a = ctl775d6;
                    v7ecb4 = ctl775da;
                } else {
                    v7ee5a = ctl775d8;
                    v7ecb4 = ctl775dc;
                }
                myplayer = cfgs[v88b9e].myplayer;
                v88c6a = cfgs[v88b9e].f08;
                nlaps = cfgs[v88b9e].laps;
                v88bb2 = cfgs[v88b9e].f10;
                v88bb6 = cfgs[v88b9e].f14;
                v88b88 = cfgs[v88b9e].f18;
                for (i = 0; i <= 7; i++) {
                    players[i].f1e = cfgs[v88b9e].f1c[i];
                    v70720[i] = cfgs[v88b9e].f3c[i];
                }
                karts[0].f6a = cfgs[v88b9e].f5c;
                karts[0].f16 = cfgs[v88b9e].f60;
                v684e0 = cfgs[v88b9e].f64;
                karts[1].f6a = cfgs[v88b9e].f68;
                karts[1].f16 = cfgs[v88b9e].f6c;
                v685e4 = cfgs[v88b9e].f70;
                v88b9e = 0;
                engines_menu(3);
            }
            if (splitscreen) {
                v88c1e = detail_lvl;
                if (v88b9e == 0 && w88c98 == 0) {
                    v88b4a = 0;
                    v88c68 = myplayer;
                    FADE();
                    v88b7e = 1;
                    cars_menu();
                    v88c6a = myplayer;
                    myplayer = v88c68;
                }
            }
            if (v88b9e) {
                pcx_load("CHECK.PCX", 0);
                if (v88c58) {
                    palptr = p7d718;
                    page_draw = 0;
                    fade_out_screen_call();
                    v88bda = 1;
                }
            } else {
                if (splitscreen) {
                    if (netgame)
                        detail_lvl = w88cb8;
                    else
                        detail_lvl = v774de;
                }
                if (w88c98 == 0)
                    fn_1be3c();
                for (;;) {
                    v7d854 = v7d85c;
                again:
                    w88c08 = 0;
                    fn_17928();
                    if (v88b9e == 0) {
                        w88c98 = 0;
                        countplays = 1;
                        race_run();
                        countplays = 0;
                        voices_reset();
                    }
                    if (w88c08 != 3 || v88bb2 == 4) {
                        v88b9e = 0;
                        if (w88c98 == 0)
                            fn_1832c();
                        else {
                            v88b9e = 1;
                            w88c98 = 0;
                        }
                        if (v88b9e == 0)
                            continue;
                        pcx_load("CHECK.PCX", 0);
                        palptr = p7d718;
                        page_draw = 0;
                        fade_out_screen_call();
                        v88bda = 1;
                        break;
                    }
                    v88b88++;
                    goto again;
                }
            }
            if (splitscreen)
                detail_lvl = v88c1e;
            if (anidope)
                ani_giggles();
            break;
        case 3:
            v7ea58 = 2;
            net88c22 = 0;
            v7eca8 = v7ee5a;
            v7ee5a = 2;
            w88bce = 1;
            v88c96 = 6;
            v88c58 = 0;
            v88b9e = 0;
            v88b4a = 0;
            FADE();
            v684e0 = 0;
            v685e4 = 0;
            twoplayer = 1;
            v88c1e = detail_lvl;
            if (netgame)
                detail_lvl = w88cb8;
            else
                detail_lvl = v774de;
            if (v88b9e == 0) {
                v88b4a = 0;
                FADE();
                v88b7e = 0;
                fn_1a2f8();
                cars_menu();
                if (v88b9e == 0) {
                    v88b4a = 0;
                    v88c68 = myplayer;
                    FADE();
                    v88b7e = 1;
                    cars_menu();
                    v88c6a = myplayer;
                    myplayer = v88c68;
                }
            }
            if (v88b9e == 0) {
                for (;;) {
                    v88b9e = 0;
                    fn_193a8();
                    if (v88b9e)
                        break;
                    v88c90 = v88c96 - 6;
                    v88b88 = v88c96;
                    v7d854 = v7d85c;
                    fn_17928();
                    if (v88b9e == 0) {
                        fn_1be3c();
                        w88bce = 1;
                        music88bcc = 0;
                        countplays = 1;
                        race_run();
                        countplays = 0;
                        voices_reset();
                    }
                }
                pcx_load("CHECK.PCX", 0);
                palptr = pal7d850;
                getpalette();
                fade_out(pal7d850);
                page_draw = 0;
                v88bda = 1;
            }
            v88bb6 = 1;
            detail_lvl = v88c1e;
            v88b88 = 1;
            v7ee5a = v7eca8;
            twoplayer = 0;
            v7ea58 = v7eca8;
            break;
        case 4:
            twoplayer = 0;
            splitscreen = 0;
            v7ea58 = 2;
            net88c22 = 0;
            detail_lvl = v7ee58;
            v7eca8 = v7ee5a;
            v7ee5a = 2;
            w88bce = 1;
            v88c96 = 6;
            v88c58 = 0;
            v88b9e = 0;
            v88b4a = 0;
            FADE();
            v684e0 = 0;
            v685e4 = 0;
            netvoices = 1;
            v7ec90 = 1;
            if (v88b9e == 0) {
                v88b4a = 0;
                FADE();
                v88b7e = 0;
                fn_1a2f8();
                cars_menu();
            }
            if (v88b9e == 0) {
                digger_load();
                for (;;) {
                    v88b9e = 0;
                    fn_193a8();
                    if (v88b9e)
                        break;
                    v88c90 = v88c96 - 6;
                    v88b88 = v88c96;
                    v7d854 = v7d85c;
                    fn_17928();
                    if (v88b9e == 0) {
                        w88bce = 1;
                        music88bcc = 0;
                        countplays = 1;
                        race_run();
                        countplays = 0;
                        voices_reset();
                    }
                }
                pcx_load("CHECK.PCX", 0);
                palptr = pal7d850;
                getpalette();
                fade_out(pal7d850);
                datfind("CARS.SP");
                fread(v7d708, 0x18f00, 1, datfile);
                faces_reset();
                page_draw = 0;
                v88bda = 1;
            }
            v88bb6 = 1;
            v88b88 = 1;
            duckfame_write();
            netvoices = 0;
            v7ee5a = v7eca8;
            v7ec90 = 0;
            v7ea58 = v7eca8;
            break;
        case 5:
            musicpaused = 0;
            net88c22 = 0;
            v7ebd0 = 0;
            strcpy(&mnames[0], " MODEM           ");
            v7ebd8 = 0;
            strcpy(&mnames[0x3c], " SERIAL CABLE    ");
            v88bb0 = 0xd;
            v88c80 = 1;
            v88bda = 0;
            strcpy(&mnames[0x78], " LEAVE THIS MENU ");
            v88b9e = 0;
            v88c80 = menu(3, 0, 0, 0);
            if (v7ecc0)
                v88b9e = 1;
            if (v88b9e == 0 && v88c80 != 3) {
                v88b9e = 0;
                linktype = v88c80;
                v88c80 = 1;
                if (linktype == 1) {
                    strcpy(&mnames[0], " MAKE CALL     ");
                    strcpy(&mnames[0x3c], " WAIT FOR CALL ");
                } else {
                    strcpy(&mnames[0], " MAKE CONNECTION      ");
                    strcpy(&mnames[0x3c], " WAIT FOR CONNECTION  ");
                }
                v88bb0 = 0xe;
                v88c80 = menu(2, v88bda, 0, 0);
                if (v7ecc0)
                    v88b9e = 1;
                v88bb0 = 0xd;
                if (v88b9e)
                    goto out;
                netgame = v88c80;
                k = 0;
                net88c14 = 0;
                if (linktype == 1) {
                    v7ec50 = cfg77178;
                    if (netgame == 1) {
                        for (;;) {
                            for (j = 1; j <= 6; j++) {
                                strcpy(mrows[j], " ");
                                strcat(mrows[j], pbook[j].name);
                                while (strlen(mrows[j]) < 20)
                                    strcat(mrows[j], " ");
                            }
                            strcpy(mrows[7], " LEAVE THIS MENU");
                            while (strlen(mrows[7]) < 20)
                                strcat(mrows[7], " ");
                            v88b9e = 0;
                            v88c80 = 1;
                            k = menu(7, v88bda, 0, 0);
                            if (v7ecc0)
                                v88b9e = 1;
                            if (v88b9e || k == 7)
                                goto out;
                            strcpy(phone, pbook[k].number);
                            if (phone[0])
                                break;
                            fn_1c068();
                            msgbox("THIS ENTRY IS BLANK!", 0);
                            WAIT300();
                            fn_1c0ac();
                        }
                    }
                } else
                    v7ec50 = cfg77174;
                if ((sendfail = baud_menu()) == 0)
                    goto out;
                sendfail = 0;
                link_open();
                if (net88c14)
                    goto out;
                sendfail = 0;
                connected = 1;
                v7ebb4 = pbook[k].f00;
                if (linktype == 1) {
                    if (modem(0) == 0)
                        goto fail;
                    fn_1c068();
                    msgbox("MODEM INITIALIZED", 0);
                    WAIT300();
                    fn_1c0ac();
                    if (netgame == 1) {
                        if (dial() == 0) {
                            modem(0);
                            goto fail;
                        }
                    } else if (answer_call() == 0) {
                        modem(1);
                        goto fail;
                    }
                }
                PALSAVE();
                msgbox("SYNC IN PROGRESS", 0);
                net88c14 = net_sync();
                if (net88c14) {
                    PALREST();
                    fn_1c068();
                    msgbox("COULD NOT SYNC!!!", 0);
                    WAIT300();
                    fn_1c0ac();
                    if (linktype == 1)
                        modem(1);
                    goto fail;
                }
                PALREST();
                connected = 0;
                if (musicpaused) {
                    music_resume();
                    musicpaused = 0;
                }
                PALSAVE();
                msgbox("CHECKING VERSION INFO", 0);
                net88c14 = net_key(v7eca0, 0, 0);
                t = ticks;
                do
                    ;
                while (ticks - t < 0x46);
                PALREST();
                if (net88c14) {
                    if (linktype == 1)
                        modem(1);
                    if (comopen) {
                        com_close();
                        comopen = 0;
                    }
                    net88c22 = 0;
                    netgame = 0;
                    net7ed78 = 0;
                    break;
                }
                v7ec9c = v7eca0;
                a = 0;
                if (v88cef != v7ec9c)
                    a = 1;
                if (v88cef < v7eca0)
                    v7ec9c = v88cef;
                if (a) {
                    strcpy(str7c854, " TWO DIFFERENT VERSIONS ARE    ");
                    strcpy(str7c890, " PLAYING. THE GAME WILL SWITCH ");
                    strcpy(str7c8cc, " TO THE LOWER VERSION WHICH IS ");
                    strcpy(str7c908, "                               ");
                    if (v7ec9c == 1)
                        strcpy(str7c944, " A SHAREWARE VERSION           ");
                    if (v7ec9c == 2)
                        strcpy(str7c944, " A REGISTERED VERSION          ");
                    if (v7ec9c == 3)
                        strcpy(str7c944, " A REGISTERED AND UPGRADED     ");
                    showlines(5, 0, 0x198);
                }
                net88c22 = 0;
                net88c14 = 0;
                break;
            }
            goto out;
        fail:
            if (comopen) {
                com_close();
                comopen = 0;
            }
        out:
            if (musicpaused) {
                music_resume();
                musicpaused = 0;
            }
            net88c22 = 0;
            netgame = 0;
            net7ed78 = 0;
            break;
        case 7:
            v88bda = 0;
            s7 = 1;
            for (;;) {
                v88c80 = s7;
                ents_init();
                if (cursong != menusong)
                    music_start(menusong, 1);
                strcpy(&mnames[0], " ORDERING        ");
                strcpy(&mnames[0x3c], " INSTRUCTIONS    ");
                strcpy(&mnames[0x78], " FAMOUS DRIVERS  ");
                strcpy(&mnames[0xb4], " CREDITS         ");
                v88bb0 = 0xd;
                strcpy(&mnames[0xf0], " LEAVE THIS MENU ");
                v88b9e = 0;
                v88c80 = menu(5, v88bda, 0, 0);
                if (v7ecc0)
                    v88b9e = 1;
                if (v88bda)
                    v88bda = 0;
                if (v88b9e || v88c80 == 5)
                    break;
                s7 = v88c80;
                switch (v88c80) {
                case 1:
                    demo_mode(1);
                    v88bda = 1;
                    break;
                case 2:
                    demo_mode(2);
                    v88bda = 1;
                    break;
                case 3:
                    v88c80 = 1;
                    engines_menu(1);
                    if (v88b9e) {
                        v88b9e = 0;
                        break;
                    }
                    zones_menu(0, 0);
                    if (v88b9e) {
                        v88b9e = 0;
                        break;
                    }
                    for (;;) {
                        strcpy(&mnames[0], " AMATEUR CLASS DRIVERS ");
                        strcpy(&mnames[0x3c], " PRO CLASS DRIVERS     ");
                        strcpy(&mnames[0x78], " CHAMP CLASS DRIVERS   ");
                        strcpy(&mnames[0xb4], " TIME TRIAL DRIVERS    ");
                        v88bb0 = 0xd;
                        strcpy(&mnames[0xf0], " LEAVE THIS MENU       ");
                        v88b9e = 0;
                        v88c80 = menu(5, v88bda, 0, 0);
                        if (v7ecc0)
                            v88b9e = 1;
                        if (v88c80 == 5)
                            v88b9e = 1;
                        if (v88b9e) {
                            v88b9e = 0;
                            break;
                        }
                        if (v88c80 == 4)
                            trial_records_menu();
                        else
                            race_records_menu(v88c80);
                    }
                    break;
                case 4:
                    credits_screen();
                    break;
                }
            }
            v88bda = 0;
            v88b9e = 0;
            break;
        case 6:
            v88bda = 0;
            s6 = 1;
            for (;;) {
                strcpy(&mnames[0], " GRAPHIC DETAIL LEVELS ");
                strcpy(&mnames[0x3c], " VOLUME LEVELS         ");
                strcpy(&mnames[0x78], " RACE SCREEN OPTIONS   ");
                v88bb0 = 0xd;
                v88c80 = s6;
                strcpy(&mnames[0xb4], " LEAVE THIS MENU       ");
                v88b9e = 0;
                s6 = menu(4, 0, 0, 0);
                if (v7ecc0)
                    v88b9e = 1;
                if (v88b9e || s6 == 4)
                    break;
                switch (s6) {
                case 1:
                    screen_menu();
                    break;
                case 2:
                    volume_menu();
                    break;
                case 3:
                    k = 1;
                    for (;;) {
                        v88b9e = 0;
                        v88c80 = k;
                        if (optmap)
                            strcpy(&mnames[0], " OVERHEAD MAP  ON  ");
                        else
                            strcpy(&mnames[0], " OVERHEAD MAP  OFF ");
                        if (optclock)
                            strcpy(&mnames[0x3c], " CLOCK DISPLAY ON  ");
                        else
                            strcpy(&mnames[0x3c], " CLOCK DISPLAY OFF ");
                        if (optspeed)
                            strcpy(&mnames[0x78], " SPEEDOMETER   ON  ");
                        else
                            strcpy(&mnames[0x78], " SPEEDOMETER   OFF ");
                        strcpy(&mnames[0xb4], " LEAVE THIS MENU   ");
                        k = menu(4, 0, 0, 0);
                        if (v7ecc0)
                            v88b9e = 1;
                        if (v88b9e || k == 4)
                            break;
                        if (k == 1) {
                            if (optmap)
                                optmap = 0;
                            else
                                optmap = 1;
                        }
                        if (k == 2) {
                            if (optclock)
                                optclock = 0;
                            else
                                optclock = 1;
                        }
                        if (k == 3) {
                            if (optspeed)
                                optspeed = 0;
                            else
                                optspeed = 1;
                        }
                    }
                    break;
                }
            }
            break;
        case 8:
            demo_mode(1);
            break;
        case 9:
            return;
        }
    }
}

/* differs: near miss, 208 of 222 bytes. The game recomputes `imul ebp, edi, 0x28` at each match and
 * keeps only i (EDI); ours strength-reduces i*40 into an EBP pointer stepped by 40 in the outer loop,
 * which shifts every register after it (QUESTIONS 2, the same as fn_16f90 (b)). Eleven forms tried
 * (direct indexing, char-pointer arithmetic, goto, while, static i, declaration orders): none keeps the
 * imul; this pointer form is the closest (31 scored vs 37). iter 38: indexing the typed alias
 * `attrs[i]` directly (declaration moved above) brings back the game's `imul edi,esi,0x28` but
 * datfind's `attrs[i].f10` is still a reduced EBP pointer (81 rows); `sprattr + i * sizeof(struct
 * attr) + 0x10` reduces both (74 rows, 22 real); `(char *)&attrs[i] + 0x10`, `&sprattr[i * 40] +
 * 0x10` and four more spellings 81 (counts before iter 56c include main_menu's 52-byte jump table, which
 * followed this function while it stood in front of main_menu: 32 rows now, all real). iter 58 nmrefs:
 * the six `in sprattr` refs the game alone uses and our `+0x4 +0x6 +0xe +0x24` are one pair, not two
 * bugs - the game folds the array base into the displacement of its i*0x28 index (`imul ebp,edi,0x28`
 * 0x2134c, `movsx edx,word [ebp+0x7299c]` 0x2134f = attrs[i].w) and we add the base once and index
 * (`add edi,@` 0x21356, `movsx edx,word [edi+4]`); the six fields line up 1:1 (w, h, nframes, data x2,
 * kind), and our precomputed f10 pointer (0x21323/0x21334/0x21376) is the game's three-instruction
 * address build 0x21375-0x2137c. No behaviour difference: datfind before fread, the size in EDX, the
 * data store before both, the break out of the inner loop. */
/* ---- 0x21310, 221 bytes: loads the sprites the level's entities use (callers 0x21504, 0x2152c in
 * ents_load 0x21490). For each SPRITE.ATR record i that some ents[j].id names, w*h*nframes bytes are
 * read from the DAT entry named at +0x10 into the running buffer v7eb4c (from v7eb20), and the first
 * kind-2 record (+0) is remembered in v7eda4. */
extern char *v7eb20, *v7eb4c;
extern int v7eda4;

void ent_sprites_load(void)
{
    int i, j, size;
    struct attr *a;

    v7eda4 = -1;
    v7eb4c = v7eb20;
    for (i = 0; i < nsprattr; i++) {
        for (j = 0; j < nents; j++) {
            if (ents[j].id == i) {
                a = (struct attr *)sprattr + i;
                size = a->w * a->h * a->nframes;
                a->data = v7eb4c;
                datfind(a->f10);
                fread(a->data, size, 1, datfile);
                if (a->kind == 2 && v7eda4 == -1)
                    v7eda4 = i;
                v7eb4c += size;
                break;
            }
        }
    }
}

/* ---- 0x213f0, 159 bytes: per-entity flags after loading a level (caller 0x301b6 in race_run). The
 * iterator is the global v7d7f4. ents[].f2 = -1 marks a kind >= 2 sprite in a one-player, off-line
 * KID MODE (v88bb2 4) race; with netvoices set, every kind > 2 entity becomes sprite v7eda4. */
extern struct ent *v7d7f4;

void ents_flags(void)
{
    short i;
    short f;
    int k;

    v7d7f4 = ents;
    for (i = 0; i < nents; v7d7f4++, i++) {
        f = 0;
        if (twoplayer == 0 && v7d998 == 0 && netvoices == 0 && v88bb2 == 4) {
            k = ((struct attr *)sprattr)[v7d7f4->id].kind;
            if (k >= 2)
                f = -1;
        }
        if (netvoices != 0) {
            k = ((struct attr *)sprattr)[v7d7f4->id].kind;
            if (k > 2)
                v7d7f4->id = v7eda4;
        }
        v7d7f4->f2 = f;
    }
}

/* ---- 0x21490, 165 bytes: loads the level's entity list (caller 0x27f1d in 0x279e8): finds the DAT
 * entry named in entname (copied from 0x7d770 by the caller), reads nents and ents[], clears each
 * f2, moves x and y by 0x400, then loads their sprites. A missing entry is logged when v88bea is set. */
extern char entname[];
extern short v88bea;

void ents_load(void)
{
    int i;

    v88bd6 = datfind(entname);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(entname);
    fread(&nents, 2, 1, datfile);
    fread(ents, nents * 12, 1, datfile);
    for (i = 0; i < nents; i++) {
        ents[i].f2 = 0;
        ents[i].x += 0x400;
        ents[i].y += 0x400;
    }
    ent_sprites_load();
}

/* ---- 0x21538, 27 bytes: swaps two shorts; no caller in WW.EXE (dead code). */
void swap16(short *a, short *b)
{
    short t;

    t = *a;
    *a = *b;
    *b = t;
}

/* differs: near miss, 348 vs 330 bytes, 42 rows, and the frames agree: only the homes are permuted.
 * The game keeps err in EAX and dy in ESI and spills dx ([esp+0xc]) and n ([esp+0x10] - `mov dword
 * [esp+0x10],0` at 0x21570, `inc dword [esp+0x10]` at 0x21643); ours keeps n and err in registers and
 * spills dy there. Same statements.
 * Levers that mattered: dx computed before dy (89 -> 39 scored), `sy, sx` declared in that order, the
 * `n = 0` initializer (38). No effect: 72 declaration orders, `short t`, `register dx`, the n++ place,
 * `volatile int n` (93 - the game's in-place `inc dword [esp+0x10]` is a plain spilled local, not a
 * volatile one) and a dummy extra int local (42). The coin flip, QUESTIONS 2. */
/* ---- 0x21554, 330 bytes: Bresenham line from (lx0, ly0) to (lx1, ly1) (caller 0x28011 in 0x279e8).
 * Appends the (x, y) short pairs, both ends included, at linep and returns their count; lx0/ly0 end
 * at the far point. A steep line swaps dx/dy and sx/sy and runs the second loop. */
extern short lx0, ly0, lx1, ly1;
extern short *linep;

int line_points(void)
{
    short dy, dx, err;
    int sy, sx, t, n = 0;

    dx = lx1 - lx0;
    dy = ly1 - ly0;
    if (dx < 0) {
        sx = -1;
        dx = -dx;
    } else
        sx = 1;
    if (dy < 0) {
        dy = -dy;
        sy = -1;
    } else
        sy = 1;
    if (dx > dy) {
        err = dx >> 1;
        while (lx0 != lx1) {
            *linep++ = lx0;
            *linep++ = ly0;
            n++;
            err += dy;
            if (err > dx) {
                ly0 += sy;
                err -= dx;
            }
            lx0 += sx;
        }
    } else {
        err = dy >> 1;
        t = dx;
        dx = dy;
        dy = t;
        t = sx;
        sx = sy;
        sy = t;
        while (ly0 != ly1) {
            *linep++ = lx0;
            *linep++ = ly0;
            n++;
            err += dy;
            if (err > dx) {
                lx0 += sy;
                err -= dx;
            }
            ly0 += sx;
        }
    }
    *linep++ = lx0;
    *linep++ = ly0;
    n++;
    return n;
}

/* ---- 0x216a0, 516 bytes: what the kart at (lx0 + v88b68, ly0 + v88b6a) hits (callers in 0x21de0).
 * surface_at classifies the map point into surf_class: 10 on a kart whose f0a is clear sets f0a, clears f44
 * (and the player's f5c and the kart's f5a when f44 was set), plays voice 12 and sets f0c/f10 = 8,
 * f0e = 0x28, v88bae = 4; 3 sets v88bae = 1. Otherwise a kart with f0a clear touches the first live
 * entity (f2 != -1) within Manhattan distance 13 (v88bae 2), else any other player whose f02 is not
 * 2 (v88bae 3): curkart->f7c and the touched f2/f02 become 1. The iterators are the globals v7d7f4
 * and v7d800. */
extern short v88b68, v88b6a, v88bae, nplayers;
extern int probe_x, probe_y, surf_class;
extern struct player *v7d800;
void surface_at(void);

void kart_hit(void)
{
    short i;

    probe_x = lx0 + v88b68;
    probe_y = ly0 + v88b6a;
    surface_at();
    if (surf_class == 10 && curkart->f0a == 0) {
        if (curkart->f44 != 0) {
            curkart->f44 = 0;
            players[v88b7e].f5c = 0;
            curkart->f5a = 0;
        }
        curkart->f0a = 1;
        voice_play(12);
        curkart->f0c = 8;
        curkart->f10 = 8;
        v88bae = 4;
        curkart->f0e = 0x28;
        return;
    }
    if (surf_class == 3) {
        v88bae = 1;
        return;
    }
    if (curkart->f0a != 0)
        return;
    v7d7f4 = ents;
    for (i = 0; i < nents; i++) {
        if (v7d7f4->f2 == -1) {
            v7d7f4++;
            continue;
        }
        if ((short)(abs(v7d7f4->x - probe_x) + abs(v7d7f4->y - probe_y)) < 0xe) {
            curkart->f7c = 1;
            v88bae = 2;
            v7d7f4->f2 = 1;
            return;
        }
        v7d7f4++;
    }
    if (v88bae != 0)
        return;
    v7d800 = players;
    for (i = 0; i <= nplayers; i++) {   /* <=: the game's bound (0x2189e jle) */
        if (i == v88b7e || v7d800->f02 == 2) {
            v7d800++;
            continue;
        }
        if ((short)(abs(v7d800->x - probe_x) + abs(v7d800->y - probe_y)) < 0xe) {
            curkart->f7c = 1;
            v88bae = 3;
            v7d800->f02 = 1;
            return;
        }
        v7d800++;
    }
}

/* differs: near miss, 28 fndiff rows (13 of them masked-fixup pairs, so 2 rows are real). Cause 2
 * is FIXED here: the index as its own statement `n = v88b7e * 2;` plus `(char *)v7d800 + 0x38 + n`
 * writes it in byte arithmetic and gives the game's `lea edx,[eax*2]` then `[edx+eax+0x38]`
 * (0x21b0a-0x21b1d); the f38 read sits above `f58 = 1` as the game's does (load 0x21b1d, store
 * 0x21b22). Remaining, both dead-end classes:
 * 1. the AI path's flag lives in EDX in the game (the dying `kind` register), EBX in ours, so
 *    `f66 = 1` stores BX instead of an immediate (0x21aad-0x21aee) and the function is 2 bytes
 *    short; 24 declaration orders, `kind` itself as the flag (much worse), a second flag local
 *    (much worse), a `(char *)+0x66` store (no change) and `flag = 1` above `kind = v7d874->kind`
 *    (25 rows, but the store lands at 0x21a7b, not the game's 0x21aad) all fail: the coin flip.
 * 2. the game hoists the karts[i] index conversion `movsx edx,cx` to 0x21be1, above the `f02 = 2`
 *    store; ours emits it inside the `soundon` arm.
 * Levers that mattered: `kind != 5` / `kind != 4` arms first, and every branch falling into one
 * `v88bae = 3; return;` (the compiler duplicates the tail; per-branch stores get scheduled early). */
/* ---- 0x218a4, 1338 bytes: what the shot at (lx0, ly0) hits (callers in 0x22230); v7d874 is the shot
 * (owner, kind, f08 armed flag, x, y). Map class 3: v88bae 1. A live entity within |dx|+|dy| < 14:
 * v88bae 2. Else the first player (i <= nplayers) not out (f02 != 2) whose kart f0a is clear, within
 * 18 - not the owner unless the shot is armed and of kind 1-4 - is hit (v88bae 3): the shot stops at
 * the player. Computer drivers (ai) spin (kind 5) or lose f64 / start f58; humans in a two-player race
 * with kind <= 2 are knocked out (f02 = 2, engine sound stopped), kind 4 sets kart f14, kind 5 spins,
 * and the rest run the hit animation on that player's kart (f1a = 7, table tb7b448). */
#pragma pack(1)
struct shot {
    short owner, kind, f04, f06, f08, x, y;
    short frame, nframes, fsize;    /* 0x0E-0x12: animation (shots_move) */
    short range;                /* 0x14: kinds 0/5 stop when dist reaches it */
    short speed;                /* 0x16: -> vec_k (shot_step) */
    short dist;                 /* 0x18 */
    char *spr;                  /* 0x1A */
    int dir;                    /* 0x1E: track direction 0..0x77f (shot_step) */
};
#pragma pack()
extern struct shot shots[61];   /* 0x7F2B0, 0x22 bytes each */
#pragma aux shots "_arr7f2b0"
extern struct shot *v7d874;
extern int tb7b448[][2];
extern struct kart *v7d5ac;
extern short v88b02;

void shot_hit(void)
{
    short hit, ai, flag, kind, i;
    int n;                      /* 0x21b11 lea edx,[eax*2]: f38's byte offset (shot_hit) */

    probe_x = lx0;
    probe_y = ly0;
    surface_at();
    if (surf_class == 3) {
        v88bae = 1;
        return;
    }
    v7d7f4 = ents;
    for (i = 0; i < nents; i++) {
        if (v7d7f4->f2 == -1) {
            ++v7d7f4;
            continue;
        }
        if ((short)(abs(v7d7f4->x - probe_x) + abs(v7d7f4->y - probe_y)) < 0xe) {
            v88bae = 2;
            return;
        }
        v7d7f4++;
    }
    if (v88bae)
        return;
    v7d800 = players;
    for (i = 0; i <= nplayers; i += 1) {
        hit = 0;
        if (v7d800->f02 == 2 || karts[i].f0a != 0) {
            v7d800++;
            continue;
        }
        if ((short)(abs(v7d800->x - probe_x) + abs(v7d800->y - probe_y)) < 0x12)
            hit = 1;
        if (i == v7d874->owner) {
            if (hit) {
                if (v7d874->f08 != 0 || v7d874->kind == 0 || v7d874->kind == 5)
                    hit = 0;
            } else if (v7d874->f08 != 0)
                v7d874->f08 = 0;
        }
        if (hit) {
            if (v7d800->f58 == 0 && v7d800->f02 != 2) {
                v7d874->x = v7d800->x;
                v7d874->y = v7d800->y;
                ai = 1;
                if (twoplayer || splitscreen) {
                    ai = 0;
                    if (i > 1)
                        ai = 1;
                } else if (i == 0)
                    ai = 0;
                kind = v7d874->kind;
                if ((twoplayer || netvoices) && kind == 5)
                    kind = 0;
                if (ai) {
                    if (5 != kind) {
                        flag = 1;
                        if (netvoices != 0) {   /* 0x21ab9 je: the block runs when v7ec74 != 0 */
                            voice_play(0xff);
                            if (v7d800->face == *(int *)&v7ec6c) {
                                flag = 0;
                                v7d800->f66 = 1;
                                v7d800->f64--;
                                if (v7d800->f64 < 0)
                                    flag = 1;
                            }
                        }
                        if (flag) {
                            if (netvoices == 0)
                                voice_play(0x11);
                            n = v88b7e * 2;
                            v7d800->f5a = *(short *)((char *)v7d800 + 0x38 + n);   /* read at 0x21b1d, above the f58 store */
                            v7d800->f58 = 1;
                            v7d800->f4c = ticks2;
                        }
                    } else {
                        v7d800->f58 = 2;
                        v7d800->f4c = ticks2;
                        voice_check(0x19);
                        if (voicebusy == 0)
                            voice_play(0x19);
                    }
                } else if (karts[i].f7e == 0) {
                    flag = 0;
                    if (twoplayer && kind <= 2)
                        flag = 1;
                    if (flag) {
                        voice_play(0xd);
                        v7d800->f56 = 0;
                        v7d800->f58 = 0;
                        v7d800->f5c = 0;
                        v7d800->f02 = 2;
                        if (soundon) {
                            FX_StopSound(karts[i].fd4);
                            karts[i].fd4 = -1;
                        }
                        karts[i].f7e = 1;
                        karts[i].f44 = 0;
                        karts[i].f20 = 0;
                        karts[i].f3c = 0;
                        karts[i].f18 = 0;
                        karts[i].f42 = 0;
                    } else if (kind != 4) {
                        if (kind == 5) {
                            v7d800->f58 = 2;
                            v7d800->f4c = ticks2;
                            voice_check(0x19);
                            if (voicebusy == 0)
                                voice_play(0x19);
                        } else {
                            v7d5ac = curkart;
                            v88b02 = v88b7e;
                            v88b7e = i;
                            curkart = &karts[i];
                            voice_play(0x11);
                            sfx_play(v7d800->face);
                            curkart->f1a = 7;
                            curkart->f3c = 0;
                            curkart->f20 = 1;
                            curkart->f1e = 0;
                            curkart->f18 = 1;
                            curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                            curkart->f32 = ticks2;
                            curkart = v7d5ac;
                            v88b7e = v88b02;
                        }
                    } else {
                        if (karts[i].f14 == 0) {
                            karts[i].f14 = 1;
                            karts[i].f8a = ticks2;
                        }
                    }
                }
            }
            v88bae = 3;
            return;
        }
        v7d800++;
    }
}

/* ---- 0x21de0, 259 bytes: moves the kart along the Bresenham line (lx0, ly0) -> (lx1, ly1), calling
 * kart_hit at every point, the far end included; stops at the first point where v88bae is set
 * (caller 0x238ac in 0x237e4). lx0/ly0 end at the last point tested. */
void kart_line(void)
{
    short dx, t, sy, sx, err, dy;     /* this order gives the game's BX/CX/SI/DI */

    dx = lx1 - lx0;
    dy = ly1 - ly0;
    if (dx < 0) {
        sx = -1;
        dx = -dx;
    } else
        sx = 1;
    if (dy < 0) {
        sy = -1;
        dy = -dy;
    } else
        sy = 1;
    if (dy < dx) {
        t = dx >> 1;                  /* through the swap temp: movsx eax/sar/mov edx,eax */
        err = t;
        while (lx0 != lx1) {
            kart_hit();
            if (v88bae != 0)
                return;
            err += dy;
            if (err > dx) {
                ly0 += sy;
                err -= dx;
            }
            lx0 += sx;
        }
    } else {
        t = dy >> 1;
        err = t;
        t = dx;
        dx = dy;
        dy = t;
        t = sx;
        sx = sy;
        sy = t;
        while (ly0 != ly1) {
            kart_hit();
            if (v88bae != 0)
                return;
            err += dy;
            if (err > dx) {
                lx0 += sy;
                err -= dx;
            }
            ly0 += sx;
        }
    }
    kart_hit();
}

/* ---- 0x21ee4, 29 bytes: plots the point (lx0, ly0) with pixel_plot; no caller in WW.EXE (dead code). The
 * call is a tail jump. */
void pixel_plot(void);

void line_plot(void)
{
    spr_x = lx0;
    spr_y = ly0;
    pixel_plot();
}

/* differs (near-miss, 4 rows / 2 real): the two `err` initialisations. The game emits
 * `movsx edx,bx; sar edx,1` (int semantics straight into err's register, interleaved with the
 * loop-test load); `err = dx >> 1` goes through EAX (the shape kart_line wants), `err = dx;
 * err >>= 1` (this) gives `mov edx,ebx` + `sar dx,1`. Tried: 150 declaration orders, `dx > dy`,
 * for loops, `(int)` casts, `/ 2`, a temp; iter 16 added a 10-variant probe (direct form, `err =
 * err >> 1`, `(int)dx >> 1`, `/ 2`, each in both branches, and the dx/dy swap before or after the
 * init) and two whole-file tries. The best probe score (2 rows) was a FALSE FRIEND: swaps first
 * then `err = dy >> 1` reads the post-swap dy = the minor half, while the game's `movsx edx,cx`
 * sits at 0x21fad *before* the dx/dy rotation and takes the major half. Whole file: direct form in
 * both branches routes through EAX (13 rows); `err = dy >> 1` before the swaps in the else branch
 * alone (if branch untouched) moves the whole else block (58 rows). err stays a short local (the
 * game's `cmp dx,bx` at 0x21f87).
 * Iter 73 shape scan (an int `>> 1` of a short): `err = dx >> 1` in both arms, the else one AFTER the
 * swaps (dx is then the old dy, the same value) 4 -> 2; the row left is the movsx scheduled before
 * the swap moves in the game, after the first in ours (8 placements and swap orders probed). */
/* ---- 0x21f04, 303 bytes: draws the Bresenham line (lx0, ly0) -> (lx1, ly1) point by point with
 * pixel_plot, the far end included (callers 0x22182, 0x221b2 in 0x22034). kart_line's walk with
 * line_plot's body written out. */
void line_draw(void)
{
    short dx, t, sy, sx, err, dy;

    dx = lx1 - lx0;
    dy = ly1 - ly0;
    if (dx < 0) {
        sx = -1;
        dx = -dx;
    } else
        sx = 1;
    if (dy < 0) {
        sy = -1;
        dy = -dy;
    } else
        sy = 1;
    if (dy < dx) {
        err = dx >> 1;
        while (lx0 != lx1) {
            spr_x = lx0;
            spr_y = ly0;
            pixel_plot();
            err += dy;
            if (err > dx) {
                ly0 += sy;
                err -= dx;
            }
            lx0 += sx;
        }
    } else {
        t = dx;
        dx = dy;
        dy = t;
        t = sx;
        sx = sy;
        sy = t;
        err = dx >> 1;                /* the old dy: the game's movsx edx,cx 0x21fad before the swaps */
        while (ly0 != ly1) {
            spr_x = lx0;
            spr_y = ly0;
            pixel_plot();
            err += dy;
            if (err > dx) {
                lx0 += sy;
                err -= dx;
            }
            ly0 += sx;
        }
    }
    spr_x = lx0;
    spr_y = ly0;
    pixel_plot();
}

/* ---- 0x22034, 505 bytes: the cracked-screen effect (caller 0x2fc75 in 0x2f794): ink = 20, the
 * crack sprite at (0x76, 0x5a), then seven straight cracks drawn with line_draw. The plain for loop
 * is right: 9.5b peels pass 0 and unrolls the other six by three itself. */

void crack_draw(void)
{
    int l[10][4];                     /* x0, x1, y0, y1 */
    int i;

    ink = 0x14;
    l[0][0] = 0x77;  l[0][2] = 0x5f;  l[0][1] = 0x16;  l[0][3] = 0x29;
    l[1][0] = 0xa8;  l[1][2] = 0x5a;  l[1][1] = 0xb9;  l[1][3] = 0x35;
    l[2][0] = 0xc6;  l[2][2] = 0x5d;  l[2][1] = 0xf7;  l[2][3] = 0x2b;
    l[3][0] = 0xf7;  l[3][2] = 0x2b;  l[3][1] = 0x128; l[3][3] = 0x38;
    l[4][0] = 0xc3;  l[4][2] = 0x91;  l[4][1] = 0xf1;  l[4][3] = 0xb2;
    l[5][0] = 0xf1;  l[5][2] = 0xb2;  l[5][1] = 0xd7;  l[5][3] = 0xc2;
    l[6][0] = 0x78;  l[6][2] = 0x86;  l[6][1] = 0x2c;  l[6][3] = 0xa9;
    spr_x = 0x76;
    spr_y = 0x5a;
    spr_h = 0x54;
    spr_w = 0x3e;
    sprptr = crackdata;
    spr_draw();
    for (i = 0; i < 7; i++) {
        lx0 = l[i][0];
        lx1 = l[i][1];
        ly0 = l[i][2];
        ly1 = l[i][3];
        line_draw();
    }
}

/* ---- 0x22230, 260 bytes: kart_line for a shot - shot_hit at every point of the line (lx0, ly0) ->
 * (lx1, ly1) until v88bae is set (caller 0x25fd6 in 0x25e6c). */
void shot_line(void)
{
    short dx, t, sy, sx, err, dy;     /* this order gives the game's BX/CX/SI/DI */

    dx = lx1 - lx0;
    dy = ly1 - ly0;
    if (dx < 0) {
        sx = -1;
        dx = -dx;
    } else
        sx = 1;
    if (dy < 0) {
        sy = -1;
        dy = -dy;
    } else
        sy = 1;
    if (dy < dx) {
        t = dx >> 1;                  /* through the swap temp: movsx eax/sar/mov edx,eax */
        err = t;
        while (lx0 != lx1) {
            shot_hit();
            if (v88bae != 0)
                return;
            err += dy;
            if (err > dx) {
                ly0 += sy;
                err -= dx;
            }
            lx0 += sx;
        }
    } else {
        t = dy >> 1;
        err = t;
        t = dx;
        dx = dy;
        dy = t;
        t = sx;
        sx = sy;
        sy = t;
        while (ly0 != ly1) {
            shot_hit();
            if (v88bae != 0)
                return;
            err += dy;
            if (err > dx) {
                lx0 += sy;
                err -= dx;
            }
            ly0 += sx;
        }
    }
    shot_hit();
}

/* ---- 0x22334-0x224eb: four kart animation starters (animation f1a, phase f18, frame pointer f4e into
 * a table of int[2] rows indexed by the player). Each keeps only EDX and builds f4e through two
 * stack temporaries. */
extern int tb7b518[][2];
extern short v88b90, v88b92;

/* 0x22334 (callers 0x22609, 0x22659 in 0x224ec): animation 2 of tb7b518, f1c = 2. */
void kart_anim2(void)
{
    curkart->f1a = 2;
    curkart->f00 = 0;
    curkart->f18 = 0;
    curkart->f1c = 2;
    curkart->f4e = &tb7b518[curkart->f1a][v88b7e];
}

/* 0x22390 (no caller found): the same after clearing v88b90/v88b92 and the kart's f1e-f24, f42. */
void kart_anim2r(void)
{
    v88b92 = 0;
    curkart->f1a = 2;
    v88b90 = 0;
    curkart->f1e = 1;
    curkart->f20 = 0;
    curkart->f22 = 0;
    curkart->f24 = 0;
    curkart->f42 = 0;
    curkart->f00 = 0;
    curkart->f18 = 0;
    curkart->f1c = 2;
    curkart->f4e = &tb7b518[curkart->f1a][v88b7e];
}

/* 0x2241c (no caller found): animation 7 (phase 1) when `which` is set, else 9 (phase 2), of tb7b448. */
void kart_anim79(short which)
{
    curkart->f1e = 0;
    if (which) {
        curkart->f18 = 1;
        curkart->f1a = 7;
    } else {
        curkart->f18 = 2;
        curkart->f1a = 9;
    }
    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
    curkart->f32 = ticks2;
}

/* 0x22494 (no caller found): animation 0 of tb7b448, phase 0xe. */
void kart_anim0(void)
{
    curkart->f1a = 0;
    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
    curkart->f18 = 0xe;
    curkart->f32 = ticks2;
}

/* differs: near miss, 377 vs 374 instructions, two causes:
 * 1. the game has one 4-byte stack slot, shared by the f32 value of the tick test (0x22582) and the
 *    row spill of &tb7b448[f1a][pl] (0x2266b); a named `t` gets its own slot (frame 8). Without `t`
 *    the registers change: v88b7e lands in EBX, not EDX (6 declaration orders tried, all worse).
 *    New: one named local CAN carry both spills - `t = curkart->f1a * 8;` then
 *    `(char *)tb7b448 + t + pl * 4` at 0x22660 gives the game's 4-byte frame and its `[esp]` spill at
 *    0x2266b - but 42 rows: the `pl * 4` (`lea edx,[edx*4]` 0x225ab) stays late and a named local for
 *    it is worse (55). Dropping `t` from the tick test is 101.
 * 2. the two charge-start tails (f3a == 0) are cross-jumped in the game (0x22aeb -> 0x22bd5), with
 *    `v88b92 = 0` scheduled late; ours keeps both and schedules that store early (5 spellings tried).
 * BEHAV iter 57: no behaviour bug. The `#0x8` only we use is our frame (`sub esp,8` 0x224ef vs the
 * game's `sub esp,4` 0x224ef - cause 1 above). The extra curkart load 0x2265c is the shared tail of
 * the two kart_anim2 calls: the game lands at 0x22660 with EAX still holding curkart across
 * `call 0x22334` (0x22609, 0x22659) and never re-reads it; we reload it. kart_anim2 (0x22334,
 * src/ww.c:9675) writes curkart->fields but never curkart, so the reload reads the same pointer -
 * same-shape.
 * New this iter (0 kept, 5 probed): reusing `t` for both spills (`t = curkart->f1a * 8;` then
 * `(char *)tb7b448 + t + pl * 4` at 0x22660) DOES give the game's 4-byte frame and `[esp]` spill,
 * but the whole function is then 42 rows - the `pl * 4` (`lea edx,[edx*4]` 0x225ab) is still late
 * and a named local for it is worse (55); dropping `t` from the tick test is 101.
 * Levers that mattered: calling kart_anim2 through an aux alias (-oe inlined it: 395 -> 369
 * instructions) and the `pl = v88b7e` local (the game keeps it in EDX for players[] and the row). */
/* ---- 0x224ec, 1799 bytes: per-frame kart state (caller 0x297c9 in 0x290a0); runs only while the
 * player's f58 is clear and v88c44 == 2. f06 counts a short spin (v88b90/v88b92 = 0x78 one side);
 * f20 steps animation 7 (f20 1) or 9 (f20 2) back or on each tick until it ends in kart_anim2;
 * f3c is the charge phase (1 or 2, by f22/f24 against f3e, f40 counting down 10 per call); with
 * neither, a charge starts when f42 >= 100 (one-player, off-line, map class not 2). f26 decays by
 * f36 (16.16) on every path. v88b8c / v88b98 select which side's value goes to v88b92 / v88b90. */
extern short v88c44, v88b8c, v88b98;
extern int tb7b4d8[], tb7b508[];

void kart_anim2_x(void);              /* kart_anim2 under another name: -oe cannot inline a body it
                                       * does not see, and the game calls it (0x22609) */
#pragma aux kart_anim2_x "kart_anim2_"

void kart_state(void)
{
    short f;
    int pl, t;

    pl = v88b7e;
    f = 0;
    if (players[pl].f58 != 0 || v88c44 != 2)
        return;
    if (curkart->f06 != 0) {
        if (curkart->f06 == 1) {
            v88b92 = 0x78;
            v88b90 = f;
        } else {
            v88b90 = 0x78;
            v88b92 = f;
        }
        if (++curkart->f08 > 4) {
            curkart->f06 = 0;
            curkart->f3c = 0;
        }
        return;
    }
    if (curkart->f20 != 0) {
        v88b92 = f;
        v88b90 = f;
        t = curkart->f32;
        if (ticks2 - t >= 1) {
            curkart->f32 = ticks2;
            if (curkart->f20 == 1) {
                v88b92 = v88c54;
                if (--curkart->f1a < 0) {
                    curkart->f1e = 1;
                    curkart->f20 = 0;
                    curkart->f22 = 0;
                    v88b92 = 0;
                    curkart->f24 = 0;
                    v88b90 = 0;
                    curkart->f42 = 0;
                    kart_anim2_x();
                    goto decay;
                }
            } else {
                v88b90 = v88c54;
                if (++curkart->f1a > 0x10) {
                    curkart->f1e = 1;
                    curkart->f20 = 0;
                    curkart->f22 = 0;
                    v88b92 = 0;
                    curkart->f24 = 0;
                    v88b90 = 0;
                    curkart->f42 = 0;
                    kart_anim2_x();
                    goto decay;
                }
            }
            curkart->f4e = &tb7b448[curkart->f1a][pl];
        }
    decay:
        if (curkart->f26 > 0 && curkart->f36 != 0)
            curkart->f26 -= (curkart->f36 * curkart->f26) >> 16;
        if (curkart->f26 <= 0 && curkart->f1e != 0) {
            curkart->f1e = 1;
            curkart->f20 = 0;
            curkart->f22 = 0;
            v88b92 = 0;
            curkart->f24 = 0;
            v88b90 = 0;
            curkart->f42 = 0;
            kart_anim2_x();
        }
        return;
    }
    if (curkart->f3c != 0) {
        if (curkart->f3a != 0)
            curkart->f3e = curkart->f3a * 2;
        else
            curkart->f3e = f;
        v88b92 = 0;
        v88b90 = 0;
        if (v88b8c != 0) {
            v88b92 = v88c7a;
            if (curkart->f3c == 2) {
                v88b92 = 0;
                if (curkart->f22 >= 2 && curkart->f3a != 0) {
                    curkart->f3c = 0;
                    return;
                }
            }
        }
        if (v88b98 != 0) {
            v88b90 = v88c7a;
            if (curkart->f3c == 1) {
                v88b90 = 0;
                if (curkart->f24 >= 2 && curkart->f3a != 0) {
                    curkart->f3c = 0;
                    return;
                }
            }
        }
        if (curkart->f3c == 1) {
            if (curkart->f22 >= curkart->f3e) {
                voice_play(0x11);
                curkart->f1a = 7;
                curkart->f3c = 0;
                curkart->f20 = 1;
                curkart->f1e = 0;
                curkart->f18 = 1;
                curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                curkart->f32 = ticks2;
                v88b92 = v88c54;
            }
        } else {
            if (curkart->f24 >= curkart->f3e) {
                voice_play(0x11);
                curkart->f1a = 9;
                curkart->f3c = 0;
                curkart->f20 = 2;
                curkart->f1e = 0;
                curkart->f18 = 2;
                curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                curkart->f32 = ticks2;
                v88b90 = v88c54;
            }
        }
        curkart->f40 -= 10;
        if (curkart->f40 < 0)
            curkart->f3c = 0;
        if (curkart->f26 > 0 && curkart->f36 != 0)
            curkart->f26 -= (curkart->f36 * curkart->f26) >> 16;
        if (curkart->f26 > 0 && curkart->f3c != 0)
            return;
        if (curkart->f20 == 0) {
            v88b92 = 0;
            v88b90 = 0;
        }
        curkart->f22 = 0;
        curkart->f24 = 0;
        curkart->f3c = 0;
        curkart->f26 = 0;
        return;
    }
    if (curkart->f26 > 0 && curkart->f36 != 0) {
        curkart->f26 -= (curkart->f36 * curkart->f26) >> 16;
        if (curkart->f26 < 0)
            curkart->f26 = f;
    }
    if (curkart->f42 >= 100 && curkart->f44 == 0 && twoplayer == 0 && netvoices == 0 && surf_class != 2)
        f = 1;
    if (v88b8c != 0)
        v88b92 = v88c7c;
    else
        v88b92 = 0;
    if (v88b98 != 0) {
        v88b92 = 0;
        v88b90 = v88c7c;
    } else
        v88b90 = 0;
    curkart->f3c = 0;
    curkart->f20 = 0;
    if (f == 0)
        return;
    if (curkart->f22 >= curkart->f3a) {
        voice_play(0x11);
        v88b92 = 0;
        v88b90 = 0;
        if (v88b8c != 0)
            v88b92 = v88c7a;
        curkart->f3c = 1;
        curkart->f18 = 3;
        curkart->f1a = 0;
        curkart->f40 = 0x78;
        curkart->f32 = ticks2;
        curkart->f24 = 0;
        curkart->f22 = 0;
        curkart->f4e = &tb7b4d8[v88b7e];
        if (curkart->f3a == 0) {
            voice_play(0x11);
            curkart->f3c = 0;
            v88b90 = 0;
            curkart->f20 = 1;
            curkart->f1a = 7;
            curkart->f1e = 0;
            curkart->f18 = 1;
            v88b92 = 0;
            curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
            curkart->f32 = ticks2;
        }
    } else if (curkart->f24 >= curkart->f3a) {
        voice_play(0x11);
        v88b92 = 0;
        v88b90 = 0;
        if (v88b98 != 0)
            v88b90 = v88c7a;
        curkart->f1a = 6;
        curkart->f18 = 4;
        curkart->f24 = 0;
        curkart->f32 = ticks2;
        curkart->f22 = 0;
        curkart->f3c = 2;
        curkart->f40 = 0x78;
        curkart->f4e = &tb7b508[v88b7e];
        if (curkart->f3a == 0) {
            voice_play(0x11);
            curkart->f3c = 0;
            v88b90 = 0;
            curkart->f20 = 2;
            curkart->f1a = 9;
            curkart->f1e = 0;
            curkart->f18 = 2;
            v88b92 = 0;
            curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
            curkart->f32 = ticks2;
        }
    }
}

/* ---- 0x22bf4, 2993 bytes: the viewing kart against the other records of arr79a28 (0x28 bytes each,
 * p7d86c from [1], v88b78 of them; caller 0x25486 in 0x253d8). v88c12 = the first record with
 * f24 == 0 (the viewing kart's own). Each other record gets its side f1e (1 ahead, 2 behind, 3 level)
 * and lane f20 (3 within 4, 4 left, 5 right); for a kart record (f24 > 0) the driver's horn/voice and
 * per-viewer state f28-f34 run, and a touch (f1c with curkart->f7c) on an entity record (f24 == -1)
 * picks it up, a bump (f24 > 0) can knock karts out; then sparks (f46-f4a) and voice 0x15/0x16.
 * The compiler keeps v88c9e and spr_y in CX/SI for the whole function, writing them back before calls.
 * The soundon/FX_StopSound blocks are engine_stop() inlined (index loaded before the soundon test).
 * differs (49 rows at the game's 3000 bytes, fndiff): (a) stack slots: the game puts n at [esp+8],
 * lo at [esp+4], m at [esp]; ours swaps n/sx/lo (21 orders of the decl list and the oy/ox
 * statements tried, B04 best); (b) oy is computed in DI with `add di,[eax+6]` in the game, through
 * EDX/EBX in ours (ox written first: 62 -> 68; `oy = CR->f02 - 1; oy += CR->f06;`: 62 -> 83);
 * (c) index forms `[edx+eax*2+@]` vs `[eax+edx*2+@]` flip by site; (d) the tb7b418 store/f0c load
 * order at 0x230d0 (chained, pointer-temp and m-placement variants all worse) plus the EDI/EBX
 * pairing of the faces[] index at 0x2312f (6 rows, the coin flip); (e) `sub ebx,[edx+0x3c]` folded
 * in ours, loaded first in the game; (f) 0x234df ours `lea edx,[eax*2]` where the game has
 * `mov edx,[v7d800]; add eax,eax; add eax,edx`. Iter 40 levers that worked: `if (v88bf6 == 0)
 * voice_play(0x15); else voice_play(0x16);` (the game jumps away for the 0x16 case), the f20 switch
 * arms ordered 4, 5, 3, and `m = v88b7e;` moved below the f28/f2c tests so the players address is
 * recomputed from the global there (the game's `movsx eax,[v88b7e]; mov [esp],eax` 0x23139):
 * 71 -> 49 rows, size 2992 -> the game's 3000. */
struct crec {
    short f00, f02, f04, f06;
    short *f08;
    int f0c;                    /* sprite */
    int f10;                    /* depth: sprites_draw sorts records 1..v88b78 by it */
    int f14;
    short f18, f1a, f1c, f1e, f20, f22, f24;   /* f18 player, f24 > 0 marks a kart */
    char f26[0x28 - 0x26];
};
struct sndrec18 { int n; char f04[0x18 - 4]; };
struct sndrec10 { int n; char f04[0x10 - 4]; };
#define CR ((struct crec *)p7d86c)
extern struct crec crecs[];
#pragma aux crecs "_arr79a28"
extern short v88c12, v88c3c, v88bf6, v88c8c, v88c42, v88c32, v88c1a, v88bfc, v88bfe, v88c9e;
extern struct sndrec18 s7a9dc[];
extern struct sndrec10 s7b100[];
extern int tb7b418[][2];
extern short tb80ae8[];

void kart_bump(void)
{
    short oy, k, pk, i, hi, hit, lo, ox, sx, n, sy;    /* order from permute.py (iter 56): 49 -> 36 rows; hit and m land in the game's slots, the other six stay permuted */
    int a, m;

    p7d86c = arr79a28 + 0x28;
    v88bf6 = 0;
    v88c12 = 0;
    for (i = 1; i <= v88b78; i++) {
        if (CR->f24 == 0) {
            sy = CR->f02 - 1 + CR->f06;
            sx = (CR->f00 >> 1) - 1 + CR->f04;
            v88c12 = i;
            break;
        }
        p7d86c += 0x28;
    }
    if (curkart->f7e != 0 || v88c3c != 0)
        return;
    p7d86c = arr79a28 + 0x28;
    for (n = 1; n <= v88b78; n++, p7d86c += 0x28) {
        if (n == v88c12)
            continue;
        oy = CR->f02 - 1 + CR->f06;
        ox = (CR->f00 >> 1) - 1 + CR->f04;
        if (oy > sy)
            CR->f1e = 2;
        else if (oy == sy)
            CR->f1e = 3;
        else
            CR->f1e = 1;
        if (ox >= sx - 4 && ox <= sx + 4)
            CR->f20 = 3;
        else if (ox > sx)
            CR->f20 = 5;
        else
            CR->f20 = 4;
        if (CR->f24 > 0) {
            v7d800 = &players[CR->f18];
            if (v7d800->f30[v88b7e] != 0 && CR->f1a <= 2 && v7d800->f58 == 0 && v7d800->f5c == 0
                && curkart->f7e == 0 && v7d800->f02 != 2 && (CR->f1e == 3 || CR->f1e == 1)) {
                if (v88c44 == 2) {
                    if (netvoices) {
                        voice_check(v7d800->face);
                        if (voicebusy == 0)
                            voice_play(v7d800->face);
                    } else {
                        voice_check(v7d800->face);
                        if (voicebusy == 0) {
                            if (s7a9dc[v7d800->face].n >= 4) {
                                voicebusy = sfx_busy(v7d800->face);
                                if (voicebusy == 0) {
                                    sfx_play(v7d800->face);
                                    if (s7b100[v7d800->face].n > 1) {
                                        s7b100[v7d800->face].n = 0;
                                        s7a9dc[v7d800->face].n = 0;
                                    }
                                }
                            } else
                                voice_play(v7d800->face);
                        }
                    }
                }
                v7d800->f30[v88b7e] = 0;
                if (netvoices == 0)
                    v7d800->f28[v88b7e] = 1;
                if (CR->f20 == 4) {
                    v7d800->f2c[v88b7e] = 5;
                    v7d800->f34[v88b7e] = 2;
                } else {
                    v7d800->f2c[v88b7e] = 4;
                    v7d800->f34[v88b7e] = 0;
                }
            }
            if (curkart->f00 == 0 && curkart->f44 == 0 && (CR->f1e == 3 || CR->f1e == 2)) {
                if (netvoices) {
                    voice_check(8);
                    if (voicebusy == 0 && v88c44 == 2)
                        voice_play(8);
                } else if (v88c44 == 2) {
                    voice_check(8);
                    if (voicebusy == 0)
                        voice_play(8);
                    if (s7a9dc[8].n > 4) {
                        voicebusy = sfx_busy(players[v88b7e].face);
                        if (voicebusy == 0) {
                            sfx_play(players[v88b7e].face);
                            if (s7b100[players[v88b7e].face].n > 3) {
                                s7a9dc[8].n = 0;
                                s7b100[players[v88b7e].face].n = 0;
                            }
                        }
                    }
                }
                if (curkart->f0a == 0) {
                    curkart->f32 = ticks2;
                    curkart->f00 = 1;
                    if (CR->f20 == 4 || CR->f20 == 3) {
                        curkart->f18 = 6;
                        curkart->f1a = 1;
                    } else {
                        curkart->f18 = 7;
                        curkart->f1a = 3;
                    }
                    curkart->f4e = &tb7b418[curkart->f1a][v88b7e];
                    crecs[v88c12].f0c = *curkart->f4e;
                }
            }
            if (v7d800->f28[v88b7e] != 0) {
                if (v7d800->f2c[v88b7e] == 4) {
                    hi = 1;
                    lo = 0;
                } else {
                    hi = 3;
                    lo = 2;
                }
                m = v88b7e;
                CR->f0c = (int)faces[v7d800->face].img[8 + v7d800->f34[m]];
                switch (v7d800->f28[m]) {
                case 1:
                    v7d800->f34[m] += 1;
                    if (v7d800->f34[m] == hi) {
                        v7d800->f28[m] = 2;
                        v7d800->f3c[m] = ticks2;
                    }
                    break;
                case 2:
                    if (ticks2 - v7d800->f3c[m] >= 3)
                        v7d800->f28[m] = 3;
                    break;
                case 3:
                    v7d800->f34[m]--;
                    if (v7d800->f34[m] < lo)
                        v7d800->f28[m] = 0;
                    break;
                }
            }
        }
        if (CR->f1c == 0 || curkart->f7c == 0)
            continue;
        hit = 1;
        if (CR->f24 == -1) {
            v7d7f4 = &ents[CR->f22];
            k = ((struct attr *)sprattr)[v7d7f4->id].kind;
            if (k > 0) {
                if (k != 1) {
                    if (curkart->f6a < 0x63) {
                        pk = 0;
                        if (curkart->f16 == 0 || k == 2)
                            pk = 1;
                        if (pk) {
                            voice_play(myplayer);
                            hit = 0;
                            v7d7f4->f2 = -1;
                            if (k != 2)
                                curkart->f16 = k;
                            else {
                                curkart->f6a += 4;
                                if (curkart->f6a > 0x63)
                                    curkart->f6a = 0x63;
                            }
                        }
                    }
                } else if (v88c8c == 0 && splitscreen == 0) {
                    voice_play(0x16);
                    hit = 0;
                    v7d7f4->f2 = -1;
                    v88c8c = -1;
                    spr_y = CR->f06;
                    for (v88c9e = 0; v88c9e <= 2; v88c9e++) {
                        tb80ae8[v88c9e] = spr_y;
                        spr_y += 0xb;
                    }
                    v88c9e = 0;
                }
            } else if (curkart->f7e == 0 && curkart->f0a == 0 && curkart->f44 == 0 && twoplayer == 0
                       && splitscreen == 0 && netvoices == 0 && v7edb0 == 0 && curkart->f42 >= 100
                       && CR->f20 == 3) {
                voice_play(0xd);
                engine_stop(v88b7e);
                curkart->f7e = 1;
                curkart->f20 = 0;
                curkart->f3c = 0;
                curkart->f18 = 0;
                curkart->f42 = 0;
                curkart->f44 = 0;
                v88c42 = 1;
                v88c32 = 0;
                v88c1a = crecs[v88c12].f06;
            }
        }
        if (hit == 0)
            continue;
        v88bf4 = 1;
        if (CR->f24 > 0) {
            v88bf6 = 1;
            if (curkart->f42 >= 100 && curkart->f7e == 0) {
                a = 1;
                if ((twoplayer || splitscreen) && CR->f18 > 1)
                    a = 0;
                if (netvoices || v7edb0)
                    a = 0;
                if (a) {
                    if (v7d800->f38[v88b7e] == 0 || v7d800->f38[v88b7e] == 1 || v7d800->f38[v88b7e] == 7)
                        a = 1;
                    else
                        a = 0;
                }
                if (a && curkart->f44 == 0 && curkart->f0a == 0) {
                    voice_play(0xd);
                    v7d800->f56 = 0;
                    v7d800->f02 = 2;
                    v7d800->f58 = 0;
                    curkart->f20 = 0;
                    curkart->f3c = 0;
                    curkart->f18 = 0;
                    curkart->f42 = 0;
                    curkart->f7e = 1;
                    engine_stop(v88b7e);
                    if (twoplayer == 0 && splitscreen == 0 && netvoices == 0) {
                        v88c42 = 1;
                        v88c32 = 0;
                        v88c1a = crecs[v88c12].f06;
                    } else {
                        for (k = 0; k <= 1; k++) {
                            engine_stop(k);
                            karts[k].f7e = 1;
                            karts[k].f20 = 0;
                            karts[k].f3c = 0;
                            karts[k].f18 = 0;
                            karts[k].f42 = 0;
                            karts[k].f44 = 0;
                            players[k].f56 = 0;
                            players[k].f02 = 2;
                            players[k].f58 = 0;
                        }
                    }
                }
            }
        }
        if (curkart->f3c != 0) {
            curkart->f3c = 0;
            curkart->f42 = 0;
        }
        if (curkart->f20 != 0) {
            curkart->f20 = 0;
            curkart->f42 = 0;
        }
        if (v88bf6 == 0)
            voice_play(0x15);
        else
            voice_play(0x16);
        curkart->f4a = sparkdata;
        curkart->f48 = 0;
        v88bfc = CR->f1e;
        v88bfe = CR->f20;
        switch (CR->f20) {
        case 4:
            curkart->f46 = 3;
            break;
        case 5:
            curkart->f46 = 4;
            break;
        case 3:
            curkart->f46 = 3;
            break;
        }
    }
}

/* ---- 0x237a8, 57 bytes, no callers (dead code): both of v7e56c/v7e570 set, or both clear, clears both. */
extern int v7e56c, v7e570;

void pair_reset(void)
{
    if ((v7e56c && v7e570) || (!v7e56c && !v7e570)) {
        v7e56c = 0;
        v7e570 = 0;
    }
}

/* ---- 0x237e4, 2740 bytes: moves curkart for one frame (caller 0x297e8 in 0x290a0). The asm track
 * code works through globals: f283a4 sets the heading for v7d678, vec_scale scales the vector
 * (vec_x, vec_y) by vec_k into (vec_kx, vec_ky), surface_at classifies the map point (probe_x, probe_y)
 * into surf_class, path_pt_scale maps the track pair at v7d690 to a point (v7d798/v7d79c, probe_x/probe_y).
 * The kart line from (view_x, view_y) by speed v88b94 goes through kart_line; a wall (v88bae 1)
 * bounces the heading by half a turn and probes both sides (v7e570/v7e56c, cleared together by
 * the inlined pair_reset when both or neither hit) to push the kart 0x12 track steps off the wall
 * with sparks; otherwise v88b92/v88b90 drift it along the track. Ends with the surface record.
 * The push-off tail is written in both branches: the game has two copies (2740 bytes, one copy 2351).
 * differs (near-miss, 511 vs 513 insns, 7 diff blocks): the game's frame is 4 bytes and every
 * "movsx eax,X; mov [esp],eax; mov ebx,[esp]" temporary (first wall probe x and y, second probe x,
 * the drift's curpos*8, the y of both side probes after a drift) uses that one slot; our build uses
 * a second slot [esp+4] for the second probe's x and the post-drift x, and keeps the first probe's
 * y in a register. The [esp+4] spill at 0x23eca is the compiler's own (no named temp there). Tried:
 * one t, t+u, a name per use, no temps, short/int/register hit, five declaration orders (best 19
 * instrdiffs). Same family as QUESTIONS 9/10/12. Also tried (iter 30) the shot_step lever:
 * `volatile int t` with u folded into t (39 rows), with u kept (88), t and u volatile (56); the
 * frame stays 8, so the game's [esp] slot is a compiler spill, not a named local. */
extern int v7d614, v7d5e4, vec_k, vec_x, vec_y, v7d678, v7d61c, v7d5ec, v7d618, v7d5e8;
extern int v7d768, v7d76c, v7d798, v7d79c;
extern int *v7d690;
extern char *v7d6a0;
extern short vec_kx, vec_ky, v88b94, v88bd8;
extern struct surf surfs[];
extern struct surf *v7d89c, *v7d8a0;
void track_around(void);
void vec_scale(void);
void path_pt_scale(void);
void f24834(int side);

#define VEC(a, b, n) { vec_x = a; vec_k = n; vec_y = b; vec_scale(); }

void kart_move(void)
{
    int t, hit, u;
    short k;
    struct surf *sp;

    curkart->faa = 0;
    if (v88c44 != 2)
        v88b94 = 0;
    track_around();
    VEC(v7d614, v7d5e4, 0x7c);
    v88b68 = vec_kx;
    v88b6a = vec_ky;
    lx0 = view_x;
    ly0 = view_y;
    vec_k = v88b94;
    vec_scale();
    lx1 = lx0 + vec_kx;
    surf_class = 0;
    ly1 = ly0 + vec_ky;
    v88bae = 0;
    kart_line();
    view_x = lx0;
    view_y = ly0;
    if (v88bae != 0 && curkart->f0a != 0) {
        if (v88bae == 1)
            curkart->f0a = 0;
        else
            v88bae = 0;
    }
    if (v88bae != 0 || curkart->f3c != 0)
        curkart->faa = 1;
    hit = v88bae;
    if (v88bae == 1) {
        if (curkart->f3c != 0) {
            curkart->f3c = 0;
            curkart->f42 = 0;
        }
        if (curkart->f20 != 0) {
            curkart->f20 = 0;
            curkart->f42 = 0;
        }
        v7d678 += 0x3c0;
        if (v7d678 >= 0x780)
            v7d678 -= 0x780;
        track_around();
        VEC(v7d614, v7d5e4, 0x20);
        view_x += vec_kx;
        v7e570 = 0;
        view_y += vec_ky;
        v7e56c = 0;
        VEC(v7d61c, v7d5ec, 0x7c);
        t = view_x;
        probe_x = t + vec_kx;
        u = view_y;
        probe_y = u + vec_ky;
        surface_at();
        if (surf_class == 3)
            v7e570 = 1;
        VEC(v7d618, v7d5e8, 0x7c);
        t = view_x;
        probe_x = t + vec_kx;
        probe_y = vec_ky + view_y;
        surface_at();
        if (surf_class == 3)
            v7e56c = 1;
        if (hit == 1)
            pair_reset();
        if (v7e570) {
            if (curkart->f46 == 0) {
                voice_play(0x15);
                curkart->f46 = 4;
                curkart->f48 = 0;
                curkart->f4a = sparkdata;
            }
            curpos -= 0x12;
            if (curpos < 0)
                curpos += 0x780;
            v7d678 = curpos;
            track_around();
            VEC(v7d614, v7d5e4, 1);
            view_x += vec_kx;
            view_y += vec_ky;
        }
        if (v7e56c) {
            if (curkart->f46 == 0) {
                voice_play(0x15);
                curkart->f46 = 3;
                curkart->f48 = 0;
                curkart->f4a = sparkdata;
            }
            curpos += 0x12;
            if (curpos >= 0x780)
                curpos -= 0x780;
            v7d678 = curpos;
            track_around();
            VEC(v7d614, v7d5e4, 1);
            view_x += vec_kx;
            view_y += vec_ky;
        }
        curkart->f42 = 0;
    } else {
        if (v88b92 > 0) {
            path_pt_scale();
            v7d798 = probe_x;
            v7d79c = probe_y;
            if (curkart->f06 != 0)
                curkart->f52 = 0x10;
            else {
                curkart->f52++;
                if (curkart->f52 > 4)
                    curkart->f52 = 4;
            }
            curpos -= v88b92;
            if (curpos < 0)
                curpos += 0x780;
            t = curpos * 8;
            v7d690 = (int *)(t + v7d6a0);
            vec_x = *v7d690++;
            vec_y = *v7d690--;
            path_pt_scale();
            v7d768 = v7d798 - probe_x;
            v7d76c = v7d79c - probe_y;
            v7d798 = probe_x;
            v7d79c = probe_y;
            view_x += v7d768;
            view_y += v7d76c;
            f24834(0);
        } else {
            curkart->f52--;
            if (curkart->f52 < 0)
                curkart->f52 = 0;
        }
        if (v88b90 > 0) {
            path_pt_scale();
            v7d798 = probe_x;
            v7d79c = probe_y;
            if (curkart->f06 != 0)
                curkart->f54 = 0x10;
            else {
                curkart->f54++;
                if (curkart->f54 > 4)
                    curkart->f54 = 4;
            }
            curpos += v88b90;
            if (curpos >= 0x780)
                curpos -= 0x780;
            v7d690 = (int *)(v7d6a0 + curpos * 8);
            vec_x = *v7d690++;
            vec_y = *v7d690--;
            path_pt_scale();
            v7d768 = v7d798 - probe_x;
            v7d76c = v7d79c - probe_y;
            v7d798 = probe_x;
            v7d79c = probe_y;
            view_x += v7d768;
            view_y += v7d76c;
            f24834(1);
        } else {
            curkart->f54--;
            if (curkart->f54 <= -1)
                curkart->f54 = 0;
        }
        v7e570 = 0;
        v7e56c = 0;
        track_around();
        VEC(v7d61c, v7d5ec, 0x7c);
        probe_x = vec_kx + view_x;
        t = view_y;
        probe_y = t + vec_ky;
        surface_at();
        if (surf_class == 3)
            v7e570 = 1;
        VEC(v7d618, v7d5e8, 0x7c);
        probe_x = vec_kx + view_x;
        t = view_y;
        probe_y = t + vec_ky;
        surface_at();
        if (surf_class == 3)
            v7e56c = 1;
        if (hit == 1)
            pair_reset();
        if (v7e570) {
            if (curkart->f46 == 0) {
                voice_play(0x15);
                curkart->f46 = 4;
                curkart->f48 = 0;
                curkart->f4a = sparkdata;
            }
            curpos -= 0x12;
            if (curpos < 0)
                curpos += 0x780;
            v7d678 = curpos;
            track_around();
            VEC(v7d614, v7d5e4, 1);
            view_x += vec_kx;
            view_y += vec_ky;
        }
        if (v7e56c) {
            if (curkart->f46 == 0) {
                voice_play(0x15);
                curkart->f46 = 3;
                curkart->f48 = 0;
                curkart->f4a = sparkdata;
            }
            curpos += 0x12;
            if (curpos >= 0x780)
                curpos -= 0x780;
            v7d678 = curpos;
            track_around();
            VEC(v7d614, v7d5e4, 1);
            view_x += vec_kx;
            view_y += vec_ky;
        }
    }
    v7d690 = (int *)(curpos * 8 + v7d6a0);
    vec_k = 0x7c;
    vec_x = *v7d690++;
    vec_y = *v7d690--;
    vec_scale();
    probe_x = view_x + vec_kx;
    probe_y = view_y + vec_ky;
    surface_at();
    v88b68 = probe_x;
    v88b6a = probe_y;
    curkart->f56 = surf_class;
    sp = &surfs[surf_class];
    curkart->f76 = sp;
    curkart->f36 = sp->f120;
    v7d89c = sp;
    curkart->f3a = sp->f124;
    if ((surf_class == 6 || surf_class == 0xf) && curkart->f0a == 0 && curkart->f44 == 0
        && curkart->f7e == 0) {
        voice_play(0xf);
        k = surf_class;
        v88bd8 = k;
        players[v88b7e].f5c = 1;
        players[v88b7e].f28[0] = 0;
        players[v88b7e].f28[1] = 0;
        curkart->f5a = 1;
        curkart->f5c = 0;
        curkart->f5e = 0;
        curkart->f44 = 1;
        v7d8a0 = &surfs[k];
        curkart->f60 = v7d8a0->f126;
        curkart->f64 = v7d8a0->f12a;
    }
}

/* ---- 0x24298, 99 bytes, no callers (dead code): kart_hit's map-class-10 block on its own. */
void kart_class10(void)
{
    if (curkart->f44 != 0) {
        curkart->f44 = 0;
        players[v88b7e].f5c = 0;
        curkart->f5a = 0;
    }
    curkart->f0a = 1;
    voice_play(12);
    curkart->f0c = 8;
    curkart->f10 = 8;
    v88bae = 4;
    curkart->f0e = 0x28;
}

/* ---- 0x242fc, 68 bytes, no callers (dead code): starts a boost once (fb4): f42 = 0xa0, fb6 = 0x10,
 * speed f26 = tbl7e730[f42], voice 12. */
extern short tbl7e730[];

void kart_boost(void)
{
    if (curkart->fb4 == 0) {
        curkart->fb4 = 1;
        curkart->f42 = 0xa0;
        curkart->fb6 = 0x10;
        curkart->f26 = tbl7e730[curkart->f42];
        voice_play(12);
    }
}

/* ---- 0x24364, 1232 bytes: fires curkart's weapon (callers 0x325de, 0x32718 in 0x30aae). joy88b42 is
 * the fire key: held for 20 ticks2 without a weapon it makes the driver's call and sets weapon 8.
 * The weapon is f16 (0: from the fe0 bits) or the ammo count f6a; nothing: voice 0x1a. Each kind
 * sets the shot sprite v7eb04, its mode v88bc0 and the shot's track position v7eb9c (read by
 * shot_fire from memory) and launches shot_fire(kind, player); then the weapon voice and f16 = 0 or
 * f6a--. players[].f12 blocks firing until the shot is gone.
 * differs (near-miss, 254 vs 229 insns, 22 diff blocks): the game's build keeps the global v7eb9c in ECX for the
 * whole function (load at entry, a store before every call and every exit, a reload after each
 * call), ~25 instructions our 9.5b build does not emit; with ECX taken, the voice number v is in SI
 * (ours CX) and ticks2 - f8e goes through EBX. Everything else lines up block for block.
 * iter 39: the load sits at entry (0x24368) and again after the first return test (0x24388) though
 * nothing loops back there, and every exit reloads ECX from v7eb9c just before `pop ecx` (0x247fe,
 * 0x24827): dead loads. `static int v7eb9c` (no address taken, so pointer stores cannot alias it):
 * no change. Not the minimap lever (the source already computes in the global).
 * iter 67 compared the 9 arm blocks of jt_24340 (0x24340) with `fixsites.py --arms kart_fire`: 0 bugs. The
 * pairs differ only in the register that holds v (ESI in the game), in the game carrying the stored value in
 * ECX (`mov ecx,1` 0x2459c then `mov [v7eb9c],ecx` 0x245bb after the call in between, where we store the
 * immediate before it; the value each call sees is the same), in its one hoisted
 * `mov ecx,[curpos]` 0x24613 above the i test our C evaluates inside the branch, and in the order of the two
 * independent stores to v88bc0 and v7eb04; table slots 7 and 8 are the same target, the default 0x247c4. */
extern int v7eb9c;
extern char *v7eb04;
extern short v88bc0;
void shot_fire(short kind, short player);

void kart_fire(void)
{
    short k, i, v, a;

    if (joy88b42 != 0 && curkart->f7e != 0)
        return;
    if (joy88b42 == 0)
        curkart->f8e = ticks2;
    if (joy88b42 == 0 && curkart->f80 != 0)
        curkart->f80 = 0;
    if (joy88b42 != 0) {
        if (curkart->f80 == 0) {
            curkart->f80 = 1;
            curkart->f8e = ticks2;
        } else
            joy88b42 = 0;
        if (ticks2 - curkart->f8e >= 0x14) {
            curkart->f8e = ticks2;
            if (curkart->f16 != 8 && v7e574 == 0) {
                voice_play(myplayer);
                curkart->f16 = 8;
            }
            joy88b42 = 0;
        }
    }
    if (joy88b42 == 0 || curkart->f0a != 0 || players[v88b7e].f12 != 0)
        return;
    a = 0;
    if (curkart->f16 != 0 || curkart->f6a != 0)
        a = 1;
    if (a != 0) {
        k = curkart->f16;
        if (k == 0) {
            if (curkart->fe0 & 4)
                k = 0;
            if (curkart->fe0 & 0x10)
                k = 3;
            if (curkart->fe0 & 8)
                k = 8;
        }
        switch (k) {
        case 0:
            v88bc0 = 3;
            v7eb04 = hm7d740;
            v7eb9c = 0;
            shot_fire(0, v88b7e);
            players[v88b7e].f12 = 1;
            v = 0x14;
            break;
        case 3:
            v88bc0 = 1;
            v7eb04 = hm7d73c;
            v7eb9c = 0;
            shot_fire(0, v88b7e);
            v88bc0 = 1;
            v7eb04 = hm7d73c;
            v7eb9c = 1;
            shot_fire(0, v88b7e);
            v7eb9c = 2;
            shot_fire(0, v88b7e);
            players[v88b7e].f12 = 3;
            v = 0x13;
            break;
        case 4:
            v = 0x17;
            for (i = 1; i <= 2; i++) {
                if (i == 1) {
                    v7eb9c = curpos + 0x1e0;
                    if (v7eb9c >= 0x780)
                        v7eb9c -= 0x780;
                } else {
                    v7eb9c = curpos - 0x1e0;
                    if (v7eb9c < 0)
                        v7eb9c += 0x780;
                }
                v88bc0 = 1;
                v7eb04 = hm7d738;
                shot_fire(1, v88b7e);
            }
            players[v88b7e].f12 = 0;
            break;
        case 5:
            v = 0x17;
            v7eb9c = curpos + 0x3c0;
            if (v7eb9c >= 0x780)
                v7eb9c = curpos - 0x3c0;
            v88bc0 = 1;
            v7eb04 = hm7d734;
            shot_fire(2, v88b7e);
            players[v88b7e].f12 = 0;
            break;
        case 6:
            v = 0x17;
            v7eb9c = curpos + 0x3c0;
            if (v7eb9c >= 0x780)
                v7eb9c = curpos - 0x3c0;
            v88bc0 = 1;
            v7eb04 = hm7d730;
            shot_fire(3, v88b7e);
            players[v88b7e].f12 = 0;
            break;
        case 7:
            v = 0x17;
            v7eb9c = curpos + 0x3c0;
            if (v7eb9c >= 0x780)
                v7eb9c = curpos - 0x3c0;
            v88bc0 = 1;
            v7eb04 = hm7d72c;
            shot_fire(4, v88b7e);
            players[v88b7e].f12 = 0;
            break;
        case 8:
            v88bc0 = 3;
            v7eb04 = hm7d728;
            v7eb9c = 0;
            shot_fire(5, v88b7e);
            v = 0x14;
            players[v88b7e].f12 = 0;
            break;
        }
        voice_check(v);
        if (voicebusy == 0)
            voice_play(v);
        if (curkart->f16 == 0)
            curkart->f6a--;
        else
            curkart->f16 = 0;
    } else {
        voice_check(0x1a);
        if (voicebusy == 0)
            voice_play(0x1a);
    }
}

/* ---- 0x24834, 118 bytes: shifts the view offset v7d860 after a track drift (kart_move side 0 left,
 * 1 right, and 0x30a09 in race_run), only when v7d998 is 0 or 3. Left: from the left stop v7d824 jump
 * to v7d82c, subtract f52, clamp at v7d824; right: from v7d830 jump to v7d828, add f54, clamp at
 * v7d830. */
extern int v7d860, v7d824, v7d828, v7d82c, v7d830;

void f24834(int side)
{
    if (v7d998 == 0 || v7d998 == 3) {
        if ((short)side) {
            if (v7d860 == v7d830)
                v7d860 = v7d828;
            v7d860 += curkart->f54;
            if (v7d860 > v7d830)
                v7d860 = v7d830;
        } else {
            if (v7d860 == v7d824)
                v7d860 = v7d82c;
            v7d860 -= curkart->f52;
            if (v7d860 < v7d824)
                v7d860 = v7d824;
        }
    }
}

/* ---- 0x248ac, 36 bytes: BIOS set video mode (caller 0x362f8). */
void setvmode(char mode)
{
    union REGS r;

    r.h.al = mode;
    r.h.ah = 0;
    int386(0x10, &r, &r);
}

/* ---- 0x248d0, 130 bytes: sets a video mode with BIOS default palette loading off (INT 10h AH=12h
 * BL=31h AL=1) around it, then on again, and sets the mode once more (callers 0x36711, 0x369dd). */
void setvmode_keep(char mode)
{
    union REGS r3, r1, r2;

    r2.h.al = 1;
    r2.h.ah = 0x12;
    r2.h.bl = 0x31;
    int386(0x10, &r2, &r2);
    r1.h.ah = 0;
    r1.h.al = mode;
    int386(0x10, &r1, &r1);
    r2.h.al = 0;
    r2.h.ah = 0x12;
    r2.h.bl = 0x31;
    int386(0x10, &r2, &r2);
    r3.h.ah = 0;
    r3.h.al = mode;
    int386(0x10, &r3, &r3);
}

/* ---- 0x24954, 91 bytes, no callers (dead code): str7d7b8 = str7d770 followed by s. */
extern char str7d7b8[], str7d770[];

void path_join(char *s)
{
    strcpy(str7d7b8, str7d770);
    strcat(str7d7b8, s);
}

/* ---- 0x249b0, 86 bytes: integer square root, two bits per step for 16 steps, rounded to nearest
 * (callers 0x2572c, 0x25bc7, 0x26462). */
int isqrt(int n)
{
    unsigned x, rem, root, d;
    short i;

    if (n <= 1)
        return n;
    x = n;
    rem = 0;
    i = 0;
    root = 0;
    do {
        rem = (rem << 2) | ((x >> 30) & 3);
        root += root;
        d = root + root + 1;
        x <<= 2;
        if (rem >= d) {
            root++;
            rem -= d;
        }
        i++;
    } while (i < 16);
    if (n - root * root >= root - 1)
        root++;
    return root;
}

/* ---- 0x24a08, 148 bytes: is the slope (view_y - v88b22) / (view_x - v88b20), 16.16, inside the sector
 * between the slopes tbl6e858[v7d674] and tbl6e858[v7d670]? (callers 0x25664, 0x25b08, 0x263a0)
 * differs (near-miss, 57 vs 57 insns, 15 instrdiffs): scheduling only - the game computes dx in EAX
 * and moves it to ESI, and loads the two table slopes after `shl edx,16`/`test esi,esi`; our build
 * loads v7d674/v7d670 right after the first movsx and subtracts in ESI. Tried: dx/dy order, a/b
 * order, `dy <<= 16` apart, four declaration orders (all 15-16). QUESTIONS 2 family. Iter 25 added 10
 * spellings in one compile (.work/probe_is): a/b assigned after the if/else (27) or after a
 * `q = dx ? dy / dx : dy;` (36) - writing them below a branch sinks them past the whole division and
 * merge, it does not slide them into the branch's shadow; `if (dx != 0)` and `q = dy; if (dx) q = dy
 * / dx;` (18, 144 bytes); the two indices in their own locals (code identical to the control, the 14
 * is the padding row going away); reading tbl6e858[] inline in the conditions with no a/b locals (no
 * CSE across the conditions: 216 bytes, 51 rows). The head stays rotated: the game hoists
 * `test esi,esi` 0x24a31 above all four table loads, keeps each index/load pair adjacent (0x24a33 ->
 * 0x24a3f, 0x24a39 -> 0x24a46) and computes dx in EAX then copies it to its home ESI (`mov esi,eax`
 * 0x24a23), where 9.5b interleaves the two index chains and computes dx straight into ESI. */
extern short v88b20, v88b22;
extern int v7d670, v7d674, tbl6e858[];

short in_sector(void)
{
    int dy, a, b, q, r, dx;

    a = tbl6e858[v7d674];
    dx = view_x - v88b20;
    dy = (view_y - v88b22) << 16;
    b = tbl6e858[v7d670];
    if (dx)
        q = dy / dx;
    else
        q = dy;
    r = 1;
    if ((a > 0 && b > 0) || (a < 0 && b < 0)) {
        if (q < a || q > b)
            r = 0;
    } else if ((a < 0 && q < 0) || (a > 0 && q > 0)) {
        if (q < a)
            r = 0;
    } else if (q > b)
        r = 0;
    return r;
}

/* ---- 0x24a9c, 93 bytes, no callers (dead code): draws gn7c528 (11x28) above the arr79a28 record
 * p7d86c, lifted by f10 while the kart's f0a is set. */
extern char *gn7c528;

void rec_marker(void)
{
    spr_x = CR->f04 + 5;
    spr_y = CR->f06 + 0xc;
    if (curkart->f0a != 0)
        spr_y -= curkart->f10;
    spr_h = 0x1c;
    spr_w = 0xb;
    sprptr = gn7c528;
    spr_draw();
}

/* ---- 0x24afc, 208 bytes: draws a two-cell marker (13x18 frames of 0xea bytes) at (x+5, y+11) and
 * 11 to the right when v88be0 is set (caller 0x25086 in 0x24bcc): mode 0 with n 0..10 hm7d724,
 * mode 1 hm7d720 (voice 0x18 unless curkart->f44), n > 100 hm7d71c. */
extern short v88be0;

void marker_draw(short n, short y, short x, short frame, short mode)
{
    short ok;

    ok = 0;
    if (mode == 0) {
        if (n >= 0 && n <= 10) {
            sprptr = hm7d724;
            ok = 1;
        }
    } else if (mode <= 1) {
        if (curkart->f44 == 0) {
            voice_check(0x18);
            if (voicebusy == 0)
                voice_play(0x18);
        }
        sprptr = hm7d720;
        ok = 1;
    }
    if (n > 100) {
        sprptr = hm7d71c;
        ok = 1;
    }
    if (v88be0 && ok) {
        spr_h = 0x12;
        spr_w = 0xd;
        spr_x = x + 5;
        spr_y = y + 0xb;
        sprptr += frame * 0xea;
        spr_draw();
        spr_x += 0xb;
        spr_draw();
    }
}

/* ---- 0x24bcc, 2060 bytes: draws curkart from its arr79a28 record p7d86c (caller 0x254b0 in 0x253d8):
 * the kart lifted by f10 while f0a (1 up to f0e, 2 down, then 3 and voice 0x17), the 4-frame flash
 * f5a/f5c from f60 at (0x8d,0xa8), rec_marker (inlined), the surface animations: v7d868 walks the six
 * 8-byte sprite sets of v7d884/v7d888 for the kart's sprite; found: surface animation 1 (f100, frames
 * f126, f108 bytes apart, f112 of them) at +0x10, else marker_draw and animation 2 (f104/f12a/f114/f11e,
 * 0x18 wide ones twice with voice 0x10); k1/k2[track-1] == 3 keeps the previous one (f6e/f72). Then
 * the three tb80ae8 markers and the sparks f46/f4a. p2offset lifts everything for player 2.
 * differs (near-miss, 434 vs 434 insns, 8 blocks): 4 of the 17 rows are the masked-displacement
 * artefact (0x25056/0x25067/0x2506e/0x252d9) and 1 is tail padding. The 12 real rows are three
 * register homes: EBX/ECX for f7a and the stride in both products (0x24f53/0x24f5c); the game keeping
 * the v7d89c base in EAX while f7a already sits in EBX (0x250ff/0x25103) where ours chains
 * `mov ebx,[v7d89c]` + `movsx ebx,[ebx+0x7a]` (0x2510c); and EAX/EDX in the f46 spark x (0x2536c-
 * 0x25375). The game also keeps v7d868 in ESI (so does ours).
 * Left: register choice in the two frame*size products (game loads f7a first in both; tried all four
 * operand orders, best kept), the f46 spark x (game `lea eax,[ebx-1]`, ours `dec edx`; six forms
 * tried) and the tb80ae8 operand (probably fixup noise). New this iter: an `int h` local for
 * `CR->f00 >> 1` (so the -1 could become the game's `lea eax,[ebx-1]`) costs 12 bytes of spills and
 * takes the function 17 -> 28 rows; not kept. Fixed on the way: spr_y -= f10 written in
 * both lift arms, the f5a==1 test nested, `sprptr == sets->spr`, spr_y before spr_x. */
struct sprset { char *spr; short f04, f06; };
extern struct sprset *sets;
#pragma aux sets "_v7d868"
extern short v88bfa;
extern char *gn7c52c;
void drawsprite_clip(void);

void kart_draw(void)
{
    short two, side, i, j;

    two = 0;
    if (twoplayer || splitscreen)
        two = 1;
    spr_h = CR->f00;
    spr_w = CR->f02;
    spr_x = CR->f04;
    spr_y = CR->f06;
    p7d838 = CR->f08;
    sprptr = (char *)CR->f0c;
    if (v88b7e && two)
        spr_y -= p2offset;
    v7d878 = CR->f14;
    if (curkart->f0a != 0) {
        if (curkart->f0a == 1) {
            curkart->f10 += 8;
            if (curkart->f10 > curkart->f0e)
                curkart->f10 = curkart->f0e;
            spr_y -= curkart->f10;
        } else {
            curkart->f10 -= 8;
            if (curkart->f10 <= 0) {
                curkart->f10 = 0;
                curkart->f0a = 3;
                voice_check(0x17);
                if (voicebusy == 0)
                    voice_play(0x17);
            }
            spr_y -= curkart->f10;
        }
    }
    if (curkart->f5a != 0 && curkart->f7e == 0) {
        if (curkart->f5a == 1) {
            if (curkart->f5c >= 2 && v88be0)
                spr_draw_scaled();
        } else if (v88be0)
            spr_draw_scaled();
        spr_h = 0x26;
        spr_w = 0x1c;
        spr_x = 0x8d;
        spr_y = 0xa8;
        if (v88b7e && two)
            spr_y -= p2offset;
        sprptr = (char *)curkart->f60 + curkart->f5c * 0x428;
        if (v88be0)
            spr_draw();
        curkart->f5c++;
        if (curkart->f5c == 4)
            curkart->f5a = 0;
    } else if (v88be0)
        spr_draw_scaled();
    if (v88c9e != 0 && curkart->f7e == 0 && curkart->f44 == 0 && v88be0)
        rec_marker();
    if (curkart->f0a == 0 && curkart->f44 == 0 && curkart->f7e == 0) {
        v88bfa = 0;
        v7d89c = curkart->f76;
        if (v88b7e == 0)
            sets = (struct sprset *)v7d884;
        else
            sets = (struct sprset *)v7d888;
        for (i = 0; i <= 5; i++) {
            if (sprptr == sets->spr) {
                v88bfa = 1;
                break;
            }
            sets++;
        }
        if (v88bfa) {
            if (v7d89c->k1[track - 1] == 3) {
                side = curkart->f6e;
                v7d89c = curkart->f72;
            } else
                side = v7d89c->f100;
            if (side != curkart->f6e) {
                curkart->f7a = 0;
                sprptr = (char *)v7d89c->f126;
            } else {
                curkart->f7a++;
                if (curkart->f7a == v7d89c->f112)
                    curkart->f7a = 0;
                sprptr = (char *)v7d89c->f126 + curkart->f7a * v7d89c->f108;
            }
            if (curkart->f26 == 0) {
                curkart->f7a = 0;
            } else {
                spr_y = CR->f06 + 0x10;
                spr_x = CR->f04 + sets->f04;
                spr_h = v7d89c->f10a;
                spr_w = v7d89c->f10c;
                if (v88b7e && two)
                    spr_y -= p2offset;
                if (v88be0)
                    spr_draw();
            }
        } else {
            spr_x = CR->f04;
            spr_y = CR->f06;
            if (v88b7e && two)
                spr_y -= p2offset;
            if (v88c3c == 0 && players[v88b7e].f58 == 0)
                marker_draw(curkart->f42, spr_y, spr_x, players[v88b7e].f54, players[v88b7e].f62);
            if (v7d89c->k2[track - 1] == 3) {
                side = curkart->f6e;
                v7d89c = curkart->f72;
            } else
                side = v7d89c->f104;
            if (side != curkart->f6e) {
                curkart->f7a = 0;
                sprptr = (char *)v7d89c->f12a;
            } else {
                curkart->f7a++;
                if (curkart->f7a == v7d89c->f11e)
                    curkart->f7a = 0;
                sprptr = (char *)v7d89c->f12a + v7d89c->f114 * curkart->f7a;
            }
            if (curkart->f26 == 0) {
                curkart->f7a = 0;
            } else {
                spr_h = v7d89c->f116;
                spr_w = v7d89c->f118;
                if (spr_w == 0x18) {
                    voice_check(0x10);
                    if (voicebusy == 0)
                        voice_play(0x10);
                    spr_y = CR->f06 + 4;
                    spr_x = CR->f04 + 1;
                    if (v88b7e && two)
                        spr_y -= p2offset;
                    if (v88c44 == 2 && v88be0) {
                        spr_draw();
                        spr_x += 0x18;
                        spr_draw();
                    }
                } else {
                    spr_y = CR->f06 + 0x14;
                    spr_x = CR->f04;
                    if (v88b7e && two)
                        spr_y -= p2offset;
                    if (v88c44 == 2 && v88be0) {
                        spr_draw();
                        spr_x += 0x1c;
                        spr_draw();
                    }
                }
            }
        }
        curkart->f6e = side;
        curkart->f72 = v7d89c;
        curkart->f76 = v7d89c;
    }
    if (v88c8c != 0 && v88c8c != -1) {
        sprptr = gn7c52c;
        spr_h = 0x1c;
        spr_w = 0xb;
        spr_x = CR->f04 + 5;
        if (v88be0)
            for (j = 0; j <= 2; j++) {
                spr_y = tb80ae8[j];
                drawsprite_clip();
            }
    }
    if (curkart->f46 != 0) {
        spr_y = CR->f06 + 0x10;
        if (v88b7e && two)
            spr_y -= p2offset;
        if (curkart->f46 == 3)
            spr_x = CR->f04;
        else if (curkart->f46 == 4)
            spr_x = CR->f04 + 0x1e;
        else
            spr_x = (CR->f00 >> 1) - 1 + CR->f04;
        spr_h = 0xa;
        spr_w = 9;
        sprptr = curkart->f4a;
        if (v88be0)
            spr_draw();
    }
    if (curkart->f0a == 3)
        curkart->f0a = 0;
}

/* ---- 0x253d8, 506 bytes: draws the scene's sprite records (callers 0x1350e, 0x13617 in frame_draw;
 * 0x289e4 jumps here): v88be0 = draw flag of player v88b7e (v7eb80/v7eb82), bubble sort of records
 * 1..v88b78 by f10 through rec_swap (swaps *swap_a and *swap_b), kart_bump, then each record: record
 * v88c12 is curkart (kart_draw); the others drawn plainly, except that with netvoices a kart record's
 * player f66 toggles and a set f66 hides it. Then the spark animation f46/f48/f4a of curkart. */
extern int swap_n;
extern struct crec *swap_tmp, *swap_a, *swap_b;
void rec_swap(void);

void sprites_draw(void)
{
    short two, i, j, hide;

    v88be0 = (&v7eb80)[v88b7e];
    swap_n = 10;
    if (twoplayer || splitscreen)
        two = 1;
    else
        two = 0;
    swap_tmp = crecs;
    for (i = 1; i <= v88b78 - 1; i++)
        for (j = i + 1; j <= v88b78; j++)
            if (crecs[i].f10 < crecs[j].f10) {
                swap_a = &crecs[i];
                swap_b = &crecs[j];
                rec_swap();
            }
    kart_bump();
    p7d86c = (char *)&crecs[1];
    for (i = 1; i <= v88b78; i++) {
        if (i == v88c12)
            kart_draw();
        else if (v88be0) {
            hide = 0;
            if (netvoices && CR->f24 > 0) {
                v7d800 = &players[CR->f18];
                if (v7d800->f64 == 0) {
                    if (v7d800->f66) {
                        hide = 1;
                        v7d800->f66 = 0;
                    } else
                        v7d800->f66 = 1;
                }
            }
            spr_h = CR->f00;
            spr_w = CR->f02;
            spr_x = CR->f04;
            spr_y = CR->f06;
            if (v88b7e && two)
                spr_y -= p2offset;
            p7d838 = CR->f08;
            sprptr = (char *)CR->f0c;
            v7d878 = CR->f14;
            if (hide == 0)
                spr_draw_scaled();
        }
        p7d86c += 0x28;
    }
    if (curkart->f46 != 0) {
        curkart->f48++;
        if (curkart->f48 == 4) {
            curkart->f46 = 0;
            curkart->f4a = sparkdata;
        } else
            curkart->f4a += 0x5a;
    }
}
/* ---- 0x255d4, 1186 bytes: adds a sprite record for every other player in view (callers frame_draw
 * 0x134e1, 0x135ea; 0x289b9): inside the box v88c16..v88c2a x v88c18..v88c2c and in_sector, rotated
 * by v7d614/v7d5e4 into depth and side, screen x 160 - side*160/(depth*tan 30), distance scaled by
 * tb6b7f8[x] picks the size step (tab6e470, 10 steps) and screen y tb6bd00[d]; sprite from the face
 * (f38 per viewer, f5a while f58 == 1) or gn7c7f0[f56] for kind 2; the split-screen car frame
 * override; clipped to 320x200. v7d800 walks players[].
 * differs (near-miss, 265 vs ~258 insns, ~130 instrdiffs): the game promotes the global v7d800 into
 * EBX for the whole loop (stored before in_sector/isqrt and at the exit, reloaded after them) and
 * keeps y and dy in two stack slots (`sub esp,8`); our build reads v7d800 from memory at every use,
 * which shifts the register allocation of the whole body. Bisecting the body down to the bounds test
 * and in_sector still gives no promotion, so it is not one statement (QUESTIONS 2, fn_193a8 point a; the
 * kart_fire family). Tried: loop forms (init order, increment order, explicit
 * increment), sx int, sy int (best), four declaration orders. in_sector returns short (`test ax,ax`).
 * iter 55 (.work/probe_pp, 10 variants in one compile, every one = control at 110): the init hoisted
 * out of the for header (`v7d800 = players;` then the for - the exact shape of the byte-identical
 * players_frames 0x287ec, which DOES cache it: `mov ebx,0x6aa90` 0x28875, `mov [v7d800],ebx` 0x28891,
 * reload 0x288c1), the comma init split both ways, `++v` increments, `volatile int sy, dy` in slots.
 * The promotion is not a loop-shape fact; the game does not cache the global in kart_hit 0x216a0
 * either (`mov dword [v7d800],0x6aa90` 0x21810, `add dword [v7d800],0x68` 0x2188f), so this is
 * register pressure in the big body. iter 58 nmrefs: no behaviour difference - a per-global access
 * census (absolute-address operands only) is 79/78: the v7d800 count lines are the EBX cache and its
 * write-backs (the game's `mov [v7d800],ebx` 0x25a68 where we keep it in memory and step it with
 * `add [v7d800],0x68` 0x25a44), p7d838's extra read is one `add dword [0x7d838],eax` 0x25943 that we
 * load-add-store (0x25930-0x25937, the addend in another register), v88b22's is our `sy` reload for
 * `if (dy < sy)` 0x256c3 where the game holds y and dy in its `sub esp,8` 0x255d9 frame
 * (`mov [esp],edx` 0x25694, `movsx eax,word [esp]` 0x256a6), and the game-only `#0x8` constant is
 * that frame - we spill nothing. */
struct carv {                   /* the 0x104-byte car records seen from 0x68400 */
    char f00[0x1a];
    short f1a;
    char f1c[0x20 - 0x1c];
    short f20;
    char f22[0x54 - 0x22];
    short f54;
    char f56[0x6e - 0x56];
    short f6e;
    char f70[0x74 - 0x70];
    char *f74;
    char f78[0x8c - 0x78];
    short f8c;                  /* cleared for cars 0 and 1 every tick (anim_tick) */
    short f8e;
    char f90[0x104 - 0x90];
};
extern struct carv carv[];
#pragma aux carv "_arr68400"
extern short v88c16, v88c18, v88c2a, v88c2c, v88ca8;
extern int tb6b7f8[];
extern short tb6bd00[];
extern char *gnspr[];
#pragma aux gnspr "_gn7c7f0"

void players_project(void)
{
    short h, sx, k, n, c, dy;
    int sy, d, side, a, m, t;

    for (v7d800 = players, v88b72 = 0; v88b72 <= nplayers; v7d800++, v88b72++) {
        if (v88ca8 == 0 && v88b72 == v88b7e)
            continue;
        v88b20 = v7d800->x;
        v88b22 = v7d800->y;
        if (v88b20 < v88c16 || v88b20 > v88c2a || v88b22 < v88c18 || v88b22 > v88c2c)
            continue;
        if (in_sector() == 0)
            continue;
        sy = v88b22;
        sx = v88b20 - view_x;
        dy = v88b22 - view_y;
        d = (v7d614 * sx + v7d5e4 * dy) >> 16;
        a = (d * 0x93cd) >> 16;
        side = (dy * v7d614 - sx * v7d5e4) >> 16;
        if (dy < sy)
            a = -a;
        c = 0xa0;
        if ((short)a != 0)
            c = 0xa0 - side * 0xa0 / (short)a;
        if (c < 0 || c > 0x13f)
            continue;
        d = isqrt(d * d + side * side);
        m = tb6b7f8[c];
        if (m) {
            t = m * d;
            d = t >> 14;
            if (t - (d << 14) >= 0x1fa0)
                d++;
        }
        if (d < 0x50 || d > 0x462)
            continue;
        k = (d - 0x7c) / 0x46;
        if (k < 0)
            k = 0;
        if (k > 9)
            k = 9;
        spr_y = tb6bd00[d];
        p7d838 = tab6e470[k];
        if (v7d800->f02 == 2) {
            if (v7d800->f56 > 3)
                continue;
            sprptr = gnspr[v7d800->f56];
        } else if (v7d800->f58 != 0 && v7d800->f58 <= 1)
            sprptr = faces[v7d800->face].img[v7d800->f5a];
        else
            sprptr = faces[v7d800->face].img[v7d800->f38[v88b7e]];
        if ((twoplayer || splitscreen) && carv[v88b72].f54 && carv[v88b72].f8e == 0) {
            p7d838 = tab6e560[k];
            sprptr = carv[v88b72].f74 + carv[v88b72].f6e * 0x300;
        }
        p7d764 = p7d838;
        h = *p7d764++;
        spr_w = *p7d764;
        spr_y -= spr_w;
        p7d838 += 2;
        spr_h = h;
        if (carv[v88b72].f1a) {
            spr_y -= carv[v88b72].f20;
            if (spr_y < 0x6e)
                spr_y = 0x6e;
        }
        if (spr_y > 0xc7)
            continue;
        c -= spr_h >> 1;
        v7d878 = 0;
        if (c < 0) {
            c = -c;
            spr_h -= c;
            if (spr_h <= 0)
                continue;
            p7d838 = (short *)((char *)p7d838 + (short)(c * (spr_w + 1) * 4));
            c = 0;
        } else if (c + spr_h - 1 > 0x13f) {
            spr_h -= spr_h - 1 + c - 0x13f;
            if (spr_h <= 0)
                continue;
        }
        if (spr_y + spr_w - 1 > 0xc7) {
            n = spr_w - 1 + spr_y - 0xc7;
            spr_w -= n;
            if (spr_w <= 0)
                continue;
            v7d878 = 4 * n;
        }
        p7d86c += 0x28;
        CR->f24 = 1;
        CR->f18 = v88b72;
        CR->f1c = v7d800->f02;
        CR->f04 = c;
        CR->f10 = d;
        CR->f1a = k;
        CR->f00 = spr_h;
        CR->f06 = spr_y;
        CR->f02 = spr_w;
        CR->f08 = p7d838;
        CR->f0c = (int)sprptr;
        v88b78 += 1;
        CR->f14 = v7d878;
    }
}

/* ---- 0x25a78, 1011 bytes: the same projection for the ents (callers frame_draw 0x134e6, 0x135ef;
 * 0x289be): every ent with f2 != -1 in the view box and in_sector gets a sprite record (f24 = f18 =
 * -1, f1c = f2, f22 = its index); sprite sprattr[id].data + frame * w*h, size tab6e470[f0c*20 + step].
 * differs (175 rows; local `struct ent *e` iterator + `v7d7f4 = e;` after the f2 guard, GOTCHAS
 * local-pointer lever, 189 -> 175: the pointer rides one register, the global gets one store per
 * iteration). Left: the game reloads v7d7f4 twice more per iteration (nmrefs game-more 6x), never
 * re-reads v88b20/v88b22, and homes the iterator in ECX and sx in DI (the players_project colourer
 * case, QUESTIONS 2). Tried: store at all three body positions (equal), reading frame/f2 through
 * the global (188), `sy = e->y; v88b22 = sy;` (187), for-init order swap (175).
 * `int i, id` (the game's `inc dword [esp+0x10]`) helped.
 * Behaviour pass (iter 61), one line per nmrefs item, no bug:
 * `#0xa` ours = our `if (k >= 10) k = 9`; the game clamps `cmp si,9 / jle` 0x25c2a + `mov esi,9`
 * 0x25c30 - same 0..9 bound, and `> 9` is already in the tried list above (it costs rows). Game
 * more 6x v7d7f4: there the ent iterator rides the global (its siblings ents_flags 0x213f0,
 * kart_hit 0x216a0, shot_hit 0x218a4, kart_bump 0x22bf4 and anim_tick 0x261fc all open
 * `v7d7f4 = ents`), and the compiler keeps it in ECX, materializing it at 0x25aab / 0x25ba3 /
 * 0x25e33 / 0x25e5b and reloading after the in_sector 0x25b08 and isqrt 0x25bc7 calls at
 * 0x25b6d / 0x25bf6 / 0x25e3d. Our local `e` differs only in the value left at the loop exit
 * (the game stores the past-the-end pointer, 0x25e5b): dead - the 23 references to 0x7d7f4 in the
 * code object sit only in those five functions and here, every one of the five writes the global
 * before its first read (0x21483, 0x2176f, 0x218dc, 0x23234, 0x26317), and the only calls here are
 * in_sector and isqrt, which do not touch it. Ours more 2x v88b22: the game loads y once (0x25b2f),
 * spills it to [esp+0xc] 0x25b38 for its `dy < sy` test 0x25b79, and does the four view-box compares
 * in registers 0x25ad4-0x25afb. Ours more 1x p7d838: the game writes p7d764 twice (0x25ca5,
 * 0x25cbe) where we step the pointer. Game more 1x nents: its `cmp word [nents],0` prologue
 * 0x25a8e, our loop test. Read equality was checked instruction by instruction over
 * 0x25a9c-0x25e39: the k clamp 0x25c12-0x25c30, tb6bd00[d] short 0x25c35, tb6b7f8[c] dword 0x25bd1,
 * tab6e470[attrs[id].f0c*0x50 + k*4] 0x25c43-0x25c6d, the clipped-row skip `+= (short)(c*(spr_w+1)
 * *4)` 0x25d24-0x25d34, and the sprite record with f22 = i 0x25e39. */
extern struct attr attrs[];
#pragma aux attrs "_sprattr"

void ents_project(void)
{
    struct ent *e;
    short sx, c, dy, n, k;
    int t, sy, i, m, d, side, id, a;

    for (e = ents, i = 0; i < nents; e++, ++i) {
        if (e->f2 == -1)
            continue;
        v7d7f4 = e;
        v88b20 = e->x;
        id = e->id;
        v88b22 = e->y;
        if (v88b20 < v88c16 || v88b20 > v88c2a || v88b22 < v88c18 || v88b22 > v88c2c)
            continue;
        if (in_sector() == 0)
            continue;
        sy = v88b22;
        sx = v88b20 - view_x;
        dy = v88b22 - view_y;
        d = (v7d614 * sx + v7d5e4 * dy) >> 16;
        side = (dy * v7d614 - sx * v7d5e4) >> 16;
        a = (d * 0x93cd) >> 16;
        if (dy < sy)
            a = -a;
        c = 0xa0;
        if ((short)a != 0)
            c = 0xa0 - side * 0xa0 / (short)a;
        if (c < 0 || c > 0x13f)
            continue;
        d = isqrt(d * d + side * side);
        m = tb6b7f8[c];
        if (m != 0) {
            t = d * m;
            d = t >> 14;
            if (t - (d << 14) >= 0x1fa0)
                ++d;
        }
        if (d < 0x50 || d > 0x462)
            continue;
        k = (d - 0x7c) / 0x46;
        if (k < 0)
            k = 0;
        if (k >= 10)
            k = 9;
        spr_y = tb6bd00[d];
        p7d838 = tab6e470[attrs[id].f0c * 20 + k];
        sprptr = attrs[id].data + e->frame * (short)(attrs[id].h * attrs[id].w);
        p7d764 = p7d838;
        p7d838 += 2;
        spr_h = *p7d764++;
        spr_w = *p7d764;
        spr_y -= spr_w;
        if (spr_y > 0xc7)
            continue;
        c -= spr_h >> 1;
        v7d878 = 0;
        if (c <= -1) {
            c = -c;
            spr_h -= c;
            if (spr_h < 1)
                continue;
            p7d838 = (short *)((char *)p7d838 + (short)(c * (spr_w + 1) * 4));
            c = 0;
        } else if (c + spr_h - 1 > 0x13f) {
            spr_h -= spr_h - 1 + c - 0x13f;
            if (spr_h <= 0)
                continue;
        }
        if (spr_y + spr_w - 1 > 0xc7) {
            n = spr_w - 1 + spr_y - 0xc7;
            spr_w -= n;
            if (spr_w <= 0)
                continue;
            v7d878 = n * 4;
        }
        p7d86c += 0x28;
        CR->f24 = -1;
        CR->f18 = -1;
        CR->f1c = e->f2;
        CR->f1a = k;
        CR->f04 = c;
        CR->f10 = d;
        CR->f00 = spr_h;
        CR->f02 = spr_w;
        CR->f06 = spr_y;
        CR->f08 = p7d838;
        CR->f0c = (int)sprptr;
        v88b78 = v88b78 + 1;
        CR->f14 = v7d878;
        CR->f22 = i;
    }
}

/* ---- 0x25e6c, 447 bytes: moves the shot v7d874 one step (callers 0x26090 in 0x2602c, 0x2d5d3):
 * heading v7eb9c = its dir for kinds 1-4, for kinds < 1 and 5 v7d5d0 turned by dir (0 straight,
 * 1 left, 2 right by 0x1e of 0x780); a kind > 1 that does not move stays put; else the step
 * v7d6a0[v7eb9c] * speed through vec_scale gives lx1/ly1. With move, shot_line tests the segment and the
 * shot stops at lx0/ly0 unless it is a kind 1 that hit (v88bae == 3).
 * differs (2 rows, probes .work/probe_ss/F,G,H): `volatile int t` gives the game's frame (`sub esp,4`)
 * and its spill of t (`mov [esp],eax; mov ebx,[esp]`) - 11 rows -> 2. What is left is the order of the
 * two loads: the game reloads t into EBX (0x25f46) and only then loads v7d6a0 into EAX (0x25f49),
 * ours the other way round. `p = t; v7d690 = (int *)(p + v7d6a0);` flips the order but swaps the
 * registers (the EAX/EBX coin flip, 3 rows); `char *b` for the base, `v7d690++` as its own statement,
 * `*(v7d690 + 1)` and the inline product without t (7 rows, no slot) do not close it. Probe I (20 more
 * spellings, .work/probe_ss/I.C) splits the two axes: every single-expression form (both operand orders,
 * `(char *)`/int-typed global, `&v7d6a0[t]`, a `char *b` or `int *base` local, a comma assignment) keeps
 * our order - global into EAX, then `mov ebx,[esp]`; every form with `p = t;` in its own statement gets
 * the game's order but pairs EAX with the slot and EBX with the global (3 rows). Same slot in
 * kart_move 0x237e4. Fixed earlier: lx1 = lx0 as its own statement lets the ly1 store be shared.
 * Iter 49: permute.py 5 rounds x 30 copies (seed 7) - best 3 = control (probe 3, fndiff 2), no
 * permutation puts the `[esp]` reload above the v7d6a0 load in their current homes. Iter 54: an
 * `extern char *volatile v7d6a0v` / `#pragma aux v7d6a0v "_v7d6a0"` alias on the other load (so both
 * memory reads are volatile) keeps our order - Watcom schedules volatile reads freely, so it pins
 * nothing here (2 rows, the two loads merely shift by 3 bytes).
 * Behaviour pass (iter 63), 1 item: the one-sided jump-target block is SAME-shape - the game
 * tail-merges the two store pairs (the lx0 arm 0x25ff1 stores x, then `jmp 0x26021` 0x26008 into the
 * shared y store that the lx1 arm 0x2600f falls into), so the block its `je 0x2600f` 0x25fef lands on
 * uses only lx1/ly1 and RET; our `return` inside `if (v88bae != 3 || kind != 1)` jumps to the
 * epilogue instead and our tail block reads v7d874 again. Same three paths and values: 0x25fe3
 * `cmp word [0x88bae],3; jne 0x25ff1` then 0x25fea `cmp word [eax+2],1; je 0x2600f` is our
 * `!= 3 ||` short-circuit, and 0x25f07/0x25f0c `test dx,dx` + `test bx,bx` are `if (move && stay)`.
 * refs 44/44 and the consts already matched, so nothing else was open. */
extern int v7d5d0;

void shot_step(short move)
{
    short stay;
    volatile int t;

    stay = 0;
    if (v7d874->kind >= 1 && v7d874->kind != 5) {
        if (v7d874->kind > 1)
            stay = 1;
        v7eb9c = v7d874->dir;
    } else
        switch (v7d874->dir) {
        case 0:
            v7eb9c = v7d5d0;
            break;
        case 1:
            v7eb9c = v7d5d0 - 0x1e;
            if (v7eb9c < 0)
                v7eb9c += 0x780;
            break;
        case 2:
            v7eb9c = v7d5d0 + 0x1e;
            if (v7eb9c >= 0x780)
                v7eb9c -= 0x780;
            break;
        }
    if (move && stay) {
        lx0 = v7d874->x;
        lx1 = lx0;
        ly0 = v7d874->y;
        ly1 = ly0;
    } else {
        t = v7eb9c * 8;
        v7d690 = (int *)(t + v7d6a0);
        vec_x = *v7d690++;
        vec_y = *v7d690;
        lx0 = v7d874->x;
        ly0 = v7d874->y;
        vec_k = v7d874->speed;
        vec_scale();
        lx1 = lx0 + vec_kx;
        ly1 = ly0 + vec_ky;
    }
    if (move) {
        surf_class = 0;
        v88bae = 0;
        shot_line();
        if (v88bae != 3 || v7d874->kind != 1) {
            v7d874->x = lx0;
            v7d874->y = ly0;
            return;
        }
    }
    v7d874->x = lx1;
    v7d874->y = ly1;
}

/* ---- 0x2602c, 462 bytes: moves the active shots (caller 0x327a0): animation f0e of f10 frames
 * (an exploding shot, f06 == 0xff, ends with it), shot_step from the owner's track position;
 * kind 1 bounces back (dir + 0x3c0) on v88bae 1 or 2, kinds 0 and 5 run out after range. A hit
 * (v88bae) plays voice 0x12, releases one of the owner's shots (players[].f12) and turns the shot
 * into the 3-frame hogdata explosion. v7d874 is kept in ECX (by our build too).
 * differs (near-miss, 109 vs 109 insns, 7 blocks): the bounce's vector load - the game puts dir*8 in
 * EDX and the base in EBX, reads both words before storing; ours swaps EBX/EDX and stores each in turn.
 * Tried: five forms of the load, three of the base+index, statement order. Found on the way: the kind
 * test is `kind != 1` first (kind-1 block in the else), `(int *)v7d6a0 + dir * 2` gives the game's
 * `imul edx,[ecx+0x1e],8`, `x = x + vec_kx` its load/add/store.
 * Iter 28: 20 more whole-file spellings (see nearmiss.txt) - the a6a0 int alias index-first/index-last
 * that makes track_here match, `int *p`/`*q`/`*b` locals, `t = dir`, `*p` + `[1]` instead of `*p++`,
 * `short sp`, every store order - leave this block byte-identical except for that home, so it is a
 * Watcom colouring coin-flip like net_recv's crc accumulator, not a spelling.
 * Iter 94 (tools/wcc95/vbatch.py, 10 spellings): the INDEXED pair `vec_x = v7d690[0]; vec_y =
 * v7d690[1]; v7d690++;` names the walked global's home EBX as the game has it - 12 -> 7 rows, whole
 * check unchanged at 211. Left: the p+1 copy (game `mov edx,ebx` + `add edx,4` 0x2612e/0x26136, ours
 * `lea eax,[ebx+4]` 0x26136) and the two load homes, plus the 2 tail rows. Not taken: `vec_k` moved
 * above the pointer or below the loads (79 - its position feeds the imul), `*++v7d690` and
 * `v7d690 = v7d690 + 1` (13), `*v7d690++` with `v7d690[0]` (13), `(int *)((char *)v7d6a0 + dir*8)`
 * (16, +8 bytes), a local `int *q` (22, -4 bytes).
 * Iter 95 (vbatch, 8 spellings: inc-between, inc-between-deref, postinc-pair, prefixinc-pair,
 * inc-first-neg, local-p, two-store-plus-plus, idx-pair-prefix): the game reads its second element
 * through the incremented pointer (`add edx,4` 0x26136 then `mov edx,[edx]` 0x26141) and stores the
 * global twice (0x26130, 0x2613b). Every spelling that does that - increment between the two reads,
 * `*v7d690++`/`*v7d690` (the shape shot_step 0x25f6f uses), `*++v7d690`, increment first with
 * `v7d690[-1]`, `v7d690 = v7d690 + 1` between, an `int *p` local (9) - scores 13 or worse; only the
 * indexed pair with `v7d690++` after (and `++v7d690`, same 7) keeps the walked global in EBX. The
 * doubled store and the `[edx]` load are allocator output, not source shape: left at 7. */
void shots_move(void)
{
    short i;

    for (v7d874 = shots, i = 0; i <= 0x3c; v7d874++, i++) {
        if (v7d874->f04 == 0)
            continue;
        v7d874->frame++;
        if (v7d874->frame >= v7d874->nframes) {
            v7d874->frame = 0;
            if (v7d874->f06 == 0xff)
                v7d874->f04 = 0;
        }
        if (v7d874->f06 == 0xff)
            continue;
        v7d5d0 = players[v7d874->owner].pos;
        shot_step(1);
        if (v7d874->kind != 1) {
            if (v7d874->kind == 0 || v7d874->kind == 5) {
                v7d874->speed = 100;
                v7d874->dist += vec_k;
                if (v7d874->dist >= v7d874->range)
                    v88bae = 1;
            }
        } else {
            if (v88bae == 2 || v88bae == 1) {
                v7d874->dir += 0x3c0;
                if (v7d874->dir >= 0x780)
                    v7d874->dir -= 0x780;
                v7d690 = (int *)v7d6a0 + v7d874->dir * 2;
                vec_k = v7d874->speed;
                vec_x = v7d690[0];
                vec_y = v7d690[1];
                v7d690++;
                vec_scale();
                v7d874->x = v7d874->x + vec_kx;
                v7d874->y = v7d874->y + vec_ky;
                v88bae = 0;
            }
        }
        if (v88bae) {
            voice_play(0x12);
            players[v7d874->owner].f12--;
            if (players[v7d874->owner].f12 < 0)
                players[v7d874->owner].f12 = 0;
            v7d874->f06 = 0xff;
            v7d874->nframes = 3;
            v7d874->fsize = 0xea;
            v7d874->frame = 0;
            v7d874->spr = hogdata;
        }
    }
}

/* ---- 0x261fc, 302 bytes: one animation tick (callers frame_draw 0x13689, 0x2fb73, 0x308f2): each
 * ent with f2 != -1 gets f2 = 0 and its frame stepped through sprattr nframes; players 2-8 (and the
 * first unless split) step the kind-2 sprite f56 up to 4 or the face frame f5a (0-7) while f58 == 1;
 * f02 other than 2 drops to 0, f54 counts 0-3 and f62 steps when set; cars 0/1 f8c cleared.
 * The game keeps twoplayer in DI (written back at the exit), v7d7f4 in EBX, v7d800 in EDX; so do we.
 * The ents loop is a guard clause with the pointer step at its tail (the game's `je 0x26247` targets
 * `add ebx,0xc`, the counter step sits after it): see GOTCHAS "a for tail reschedules". */
void anim_tick(void)
{
    short on, i;

    v7d7f4 = ents;
    for (i = 0; i < nents; i++) {
        if (v7d7f4->f2 == -1) {
            v7d7f4++;
            continue;
        }
        v7d7f4->f2 = 0;
        v7d7f4->frame++;
        if (v7d7f4->frame == attrs[v7d7f4->id].nframes)
            v7d7f4->frame = 0;
        v7d7f4++;
    }
    for (v7d800 = players, i = 1; i <= 8; v7d800++, i++) {
        on = 1;
        if (i == 1)
            on = 0;
        if (twoplayer || splitscreen && i == 2)
            on = 0;
        if (on) {
            if (v7d800->f02 == 2) {
                if (v7d800->f56 <= 3 && twoplayer == 0 && splitscreen == 0)
                    v7d800->f56++;
            } else if (v7d800->f58 == 1) {
                v7d800->f5a++;
                if (v7d800->f5a > 7)
                    v7d800->f5a = 0;
            }
        }
        if (v7d800->f02 != 2)
            v7d800->f02 = 0;
        v7d800->f54++;
        if (v7d800->f54 > 3) {
            v7d800->f54 = 0;
            if (v7d800->f62)
                v7d800->f62++;
        }
    }
    carv[0].f8c = 0;
    carv[1].f8c = 0;
}

/* ---- 0x2632c, 891 bytes: the same projection for the active shots (callers frame_draw 0x134ff,
 * 0x13608; 0x289d7): size tab6e600[step], sprite spr + frame * fsize, record f1c = 0, f24 = f18 = -1.
 * differs (near-miss, 117 rows, ours 844 bytes vs the game's 891): what took 155 -> 117 (iter 42) is
 * a local `struct shot *p` declared LAST as the loop pointer with `v7d874 = p;` just after the f04
 * guard (the game's `mov ecx,shots` 0x26335, `mov [v7d874],ecx` 0x26351; declaring p first 134),
 * `short sy` (its compare is 16-bit, `cmp dx,word [esp]` 0x26415) and `spr_h = *p7d764++` with no h
 * local (no slot spill). Left: the homes of the sx/sy/dy products (game sx EAX cwt, v7d614 ESI,
 * v7d5e4 EBX, d EBX->EDI, side ESI; ours permuted), the slot order (game sy [esp], dy [esp+4],
 * i [esp+8]; ours i in EBP), p in EBX not ECX, and the game's six extra v7d874 resyncs (0x26403,
 * 0x2643e, 0x26491, 0x26675, 0x2667e, 0x26697). Worse: `short a` 135 (the `(short)` casts are right),
 * `short i` 146, a volatile alias pinning the store 136, `v7d874 = p;` after the loop 120 and a
 * nmrefs DIFFER - the game leaves the global past the table (0x26697), ours the last live shot; no
 * reader uses it unset (shot_step, shot_hit, shot_fire and shots_clear all set it). Byte-identical:
 * both product orders in the f0c load, `if (!p->f04)`, dropping the unused h. iter 58 nmrefs: the
 * global-access census (absolute operands only, ours 52 / game 55) finds no behaviour difference -
 * the game's six extra v7d874 touches are its loop-pointer resyncs (0x26403, 0x2643e, 0x26491,
 * 0x26675, 0x2667e, 0x26697) around calls and branch joins; only the value the global holds after
 * the return differs, and it is dead (findings/game-loop.md: the readers shot_step 0x25e6c and
 * shot_hit 0x218a4 are always called after shots_move 0x2608a or shot_fire 0x2d5ad stored it), so
 * the past-the-table store 0x26697 is not copied back; the game-only `+0x22` is its precomputed
 * next-pointer `lea eax,[ecx+0x22]` 0x2651b (`sizeof struct shot`) fed to the continue paths
 * 0x26529/0x2655e, our step is the same immediate 0x22; p7d838's extra read is the game's
 * `add dword [0x7d838],4` 0x264f8 and the two v88b20/v88b22 reads are its sy/dy frame slots. */
extern short *tab6e600[];

void shots_project(void)
{
    short n, dy, sx, k, c, sy;
    int i, d, side, a, m, t;
    struct shot *p;

    for (p = shots, i = 0; i <= 0x3c; p++, i++) {
        if (p->f04 == 0)
            continue;
        v7d874 = p;
        v88b20 = p->x;
        v88b22 = p->y;
        if (v88b20 < v88c16 || v88b20 > v88c2a || v88b22 < v88c18 || v88b22 > v88c2c)
            continue;
        if (in_sector() == 0)
            continue;
        sx = v88b20 - view_x;
        sy = v88b22;
        dy = v88b22 - view_y;
        d = (v7d614 * sx + v7d5e4 * dy) >> 16;
        a = (d * 0x93cd) >> 16;
        side = (dy * v7d614 - sx * v7d5e4) >> 16;
        if (dy < sy)
            a = -a;
        c = 0xa0;
        if ((short)a != 0)
            c = 0xa0 - side * 0xa0 / (short)a;
        if (c < 0 || c > 0x13f)
            continue;
        d = isqrt(d * d + side * side);
        m = tb6b7f8[c];
        if (m) {
            t = d * m;
            d = t >> 14;
            if (t - (d << 14) >= 0x1fa0)
                d++;
        }
        if (d < 0x50 || d > 0x462)
            continue;
        k = (d - 0x7c) / 0x46;
        if (k < 0)
            k = 0;
        if (k > 9)
            k = 9;
        spr_y = tb6bd00[d];
        p7d838 = tab6e600[k];
        p7d764 = p7d838;
        spr_h = *p7d764++;
        p7d838 += 2;
        spr_w = *p7d764;
        spr_y -= spr_w;
        if (spr_y > 0xc7)
            continue;
        c -= spr_h >> 1;
        v7d878 = 0;
        if (c < 0) {
            c = -c;
            spr_h -= c;
            if (spr_h <= 0)
                continue;
            p7d838 = (short *)((char *)p7d838 + (short)(c * (spr_w + 1) * 4));
            c = 0;
        } else if (c + spr_h - 1 > 0x13f) {
            spr_h -= spr_h - 1 + c - 0x13f;
            if (spr_h <= 0)
                continue;
        }
        if (spr_y + spr_w - 1 > 0xc7) {
            n = spr_w - 1 + spr_y - 0xc7;
            spr_w -= n;
            if (spr_w <= 0)
                continue;
            v7d878 = n * 4;
        }
        p7d86c += 0x28;
        CR->f1c = 0;
        CR->f24 = -1;
        CR->f18 = -1;
        CR->f1a = k;
        CR->f04 = c;
        CR->f10 = d;
        CR->f00 = spr_h;
        CR->f02 = spr_w;
        CR->f06 = spr_y;
        CR->f08 = p7d838;
        CR->f14 = v7d878;
        v88b78++;
        CR->f0c = (int)(p->spr + p->frame * p->fsize);
    }
}

/* ---- 0x266a8, 916 bytes: the start lights (callers frame_draw 0x134fa, 0x13603 while v88c38;
 * 0x289d2): during the countdown (v88c44 == 1) the frame v88c5e goes to 1 after 0x11 ticks of ticks2
 * since v7eba8 and to 2 after 0x22, which also starts the race (v88c44 = 2) with voice 10; then the
 * lights at (v88c4a, v88c4c) are projected like a shot (tab6e470 sizes, gn7eb40[v88c5e]) and
 * v88c38 stays set only while they are on screen. LIKELY: "lights" from the 3 frames and the timing.
 * differs (85 rows, ours 920 bytes vs the game's 916): the countdown test is `cmp ax,1; jb; ja` in
 * the game (a switch range check; ours `jne`) and the rest is the projection's register homes and
 * product scheduling (game ECX/EDX/EBX/EDI, ours ECX/EBX/ESI/EDI; iter 41 probe .work/probe_lp/:
 * four product operand orders 82/82/82 vs control 82, `case 0: break;` 99, `if (sy > dy)` 104,
 * `short side` 93). ticks2 - v7eba8 is the inlined since2(): `(m = v7eba8, ticks2 - m)` gives its
 * load order. What fixed 112 -> 85: `short sy` (the game's `cmp ax,word [esp+4]` 0x267db is a
 * 16-bit compare of two slot values) and `short a` (the game's `test di,di` 0x267e9 +
 * `movsx ebx,di` + `idiv ebx` 0x26803; `idiv dword [esp]` was ours), plus `spr_h = *p7d764++`
 * instead of an `h` local + store (gives the game's second load through the updated pointer,
 * `mov ax,word [eax]` 0x268c6). Same three spellings are open in the sibling projections players_project and shots_project.
 *
 * iter 64 BEHAV census (globrefs: `counts 0x7d838 ours x5 game x4`, `counts 0x88b36 spr_h ours x8 game
 * x7`, every other address the same count): BOTH SAME, no store missing, the exit value of both globals
 * is the same. (1) p7d838: the game advances the c<0 adjustment with one in-place read-modify-write
 * `add dword [p7d838],eax` 0x2692e (one address), our build does `mov eax,[p7d838]` 0x16659 +
 * `mov [p7d838],eax` 0x16660; the other four sites match one for one (store 0x268aa/0x165d0, `add
 * [p7d838],4` 0x268ba/0x165e0 = `p7d838 += 2` on a short *, final read 0x26a00/0x16731 for CR->f08),
 * and the game's 16-bit truncation `movsx eax,bx` 0x2692b is our `(short)(c * (spr_w + 1) * 4)`. (2)
 * spr_h: the game keeps h in SI from `mov si,[eax]` 0x268b4 and reuses it for `c -= spr_h >> 1`
 * (`movsx eax,si` 0x268ec + `sar esi,1` 0x268f4) and for the second arm's bound (`dec eax; add eax,esi`
 * 0x2693b/0x2693c, EAX still = h); our build re-reads the word at 0x16613 and 0x16676. Between the
 * store `mov [spr_h],si` 0x268c9 and those reads nothing writes [0x88b36] (the neighbours spr_w
 * 0x88b26 / spr_y 0x88b34 are distinct words), so the value is identical. The one-sided verbose branch
 * line (the range-check block 0x88b20/0x88b22/0x88c16/0x88c4a/0x88c4c) is a jmp-vs-conditional flag
 * difference only - `jump-target blocks one side only: 0` means both sides jump to that same block. No
 * BUG, no code change. */
extern short v88c5e, v88c4a, v88c4c;
extern int v7eba8;
extern char *lights[];
#pragma aux lights "_gn7eb40"

void lights_project(void)
{
    short sy, dy, k, sx, n, c, h, a;
    int d, side, m, t;

    if (v88c44)
        v88c38 = 0;
    switch (v88c44) {
    case 1:
        t = (m = v7eba8, ticks2 - m);
        if (t >= 0x22) {
            v88c44 = 2;
            v88c5e = 2;
            voice_play(10);
        } else if (t >= 0x11)
            v88c5e = 1;
        break;
    }
    v88b20 = v88c4a;
    v88b22 = v88c4c;
    if (v88b20 < v88c16 || v88b20 > v88c2a || v88b22 < v88c18 || v88b22 > v88c2c)
        return;
    if (in_sector() == 0)
        return;
    sx = v88b20 - view_x;
    sy = v88b22;
    dy = v88b22 - view_y;
    d = (v7d614 * sx + v7d5e4 * dy) >> 16;
    a = (d * 0x93cd) >> 16;
    side = (dy * v7d614 - sx * v7d5e4) >> 16;
    if (dy < sy)
        a = -a;
    c = 0xa0;
    if ((short)a != 0)
        c = 0xa0 - side * 0xa0 / (short)a;
    if (c < 0 || c > 0x13f)
        return;
    d = isqrt(d * d + side * side);
    m = tb6b7f8[c];
    if (m) {
        t = d * m;
        d = t >> 14;
        if (t - (d << 14) >= 0x1fa0)
            d++;
    }
    if (d < 0x50 || d > 0x462)
        return;
    k = (d - 0x7c) / 0x46;
    if (k < 0)
        k = 0;
    if (k > 9)
        k = 9;
    spr_y = tb6bd00[d];
    p7d838 = tab6e470[k];
    p7d764 = p7d838;
    spr_h = *p7d764++;
    p7d838 += 2;
    spr_y -= *p7d764;
    spr_w = *p7d764;
    if (spr_y > 0xc7)
        return;
    c -= spr_h >> 1;
    v7d878 = 0;
    if (c < 0) {
        c = -c;
        spr_h -= c;
        if (spr_h <= 0)
            return;
        p7d838 = (short *)((char *)p7d838 + (short)(c * (spr_w + 1) * 4));
        c = 0;
    } else if (c + spr_h - 1 > 0x13f) {
        spr_h -= spr_h - 1 + c - 0x13f;
        if (spr_h <= 0)
            return;
    }
    if (spr_y + spr_w - 1 > 0xc7) {
        n = spr_w - 1 + spr_y - 0xc7;
        spr_w -= n;
        if (spr_w <= 0)
            return;
        v7d878 = n * 4;
    }
    p7d86c += 0x28;
    CR->f1c = 0;
    CR->f24 = -1;
    CR->f18 = -1;
    CR->f1a = k;
    CR->f04 = c;
    CR->f10 = d;
    CR->f00 = spr_h;
    CR->f02 = spr_w;
    CR->f06 = spr_y;
    CR->f08 = p7d838;
    CR->f14 = v7d878;
    v88c38 = 1;
    v88b78++;
    CR->f0c = (int)lights[v88c5e];
}

/* ---- 0x26a3c, 1158 bytes: the sprite record of the viewer's own kart at the bottom centre (caller
 * frame_draw 0x134fa..; between ents_project and lights_project): nothing while v88c42 == 4 or
 * v88ca8; a kind-2 viewer in a split/net game only steps its sprite f56; while f44 (engine trouble?)
 * the 8-frame f64 animation with voice 0xe until f56 changes (then voice 0xf, f5a = 2); else the
 * kart frame from f4e, and the v88c42 intro: 1 drives in (tb7b328 frames, v88c1a down by 6 to 0x72),
 * 2 waits 11 ticks, 3 drives out (tb7b2d8 frames, v88c1a up by 4 to 0xc7), 4 done. The kart bobs a
 * pixel every other frame while racing. The game keeps the global spr_y in CX throughout (so do we).
 * differs (near-miss, 233 vs 233 insns, 1 real block + fixup noise): the bottom-clip test - the game
 * puts spr_w - 1 in EDX and spr_y in EAX, ours the other way round; six forms of the sum tried.
 * Iter 22 adds one more: `spr_y + spr_w - 1` (the spelling the four other project functions use at
 * src/ww.c 11036, 11136, 11417, 11532) puts the sum in EAX from spr_w as before and costs a row,
 * 10 -> 11, so it is not the game's form here. Game 0x26df2-0x26dfc is `movsx edx,[spr_w]`,
 * `movsx eax,cx`, `dec edx`, `add eax,edx`: the sum starts from spr_y. GOTCHAS "Which pseudo gets
 * EAX is a coin flip" - the only 3 real rows of this 1160-byte function are that swap.
 * Found: `k = v88c42; switch (k)` (else the n/v88c42 registers swap), `(m = v7e72c, ticks2 - m)` for
 * the inlined since2(), `sprptr = gnspr[f56]; f56++;`, and f56 > 3 (not <= 3) for the kind-2 step. */
extern int tb7b328[], tb7b2d8[], v7e72c;

void self_project(void)
{
    short x, n, k;
    int m;

    if (v88c42 == 4 || v88ca8)
        return;
    if ((twoplayer || splitscreen || netvoices) && curkart->f7e && players[v88b7e].f56 > 3) {
        players[v88b7e].f56++;
        return;
    }
    v7d878 = 0;
    x = 0xa0;
    if (curkart->f44) {
        voice_check(0xe);
        if (voicebusy == 0)
            voice_play(0xe);
        p7d838 = tab6e560[0];
        sprptr = (char *)curkart->f64 + curkart->f5e * 0x300;
        spr_y = tb6bd00[0x7c];
        curkart->f5e++;
        if (curkart->f5e == 8)
            curkart->f5e = 0;
        if (curkart->f56 != 3 && v88bd8 != curkart->f56) {
            players[v88b7e].f5c = 0;
            curkart->f44 = 0;
            curkart->f5c = 0;
            curkart->f5a = 2;
            voice_play(0xf);
        }
    } else {
        p7d838 = tab6e470[0];
        spr_y = tb6bd00[0x7c];
        if (v88c42) {
            n = v88c32 + 1;
            k = v88c42;
            switch (k) {
            case 1:
                curkart->f4e = &tb7b328[v88c32];
                v88c32 = n;
                if (n == 8)
                    v88c32 = 0;
                v88c1a -= 6;
                if (v88c1a <= 0x72) {
                    v88c1a = 0x72;
                    v88c42 = 2;
                    curkart->f4e = tb7b2d8;
                    v88c32 = 0;
                    v7e72c = ticks2;
                }
                break;
            case 2:
                curkart->f4e = &tb7b2d8[v88c32];
                if ((m = v7e72c, ticks2 - m) >= 0xb)
                    v88c42 = 3;
                break;
            case 3:
                v88c32 = n;
                if (n == v88c4e)
                    v88c32 = 0;
                curkart->f4e = &tb7b2d8[v88c32];
                v88c1a += 4;
                if (v88c1a > 0xc7) {
                    v88c1a = 0xc7;
                    v88c42 = 4;
                    v7e72c = ticks2;
                }
                break;
            }
        }
        sprptr = (char *)*curkart->f4e;
    }
    if (curkart->f7e && v88c42 == 0) {
        curkart->f44 = 0;
        p7d838 = tab6e470[0];
        sprptr = gnspr[players[v88b7e].f56];
        players[v88b7e].f56++;
    }
    p7d764 = p7d838;
    spr_h = *p7d764++;
    spr_w = *p7d764;
    spr_y -= spr_w;
    p7d838 += 2;
    if (curkart->f26 > 0 && curkart->f44 == 0 && curkart->f7e == 0 && v88c44 == 2) {
        if (curkart->f68) {
            spr_y--;
            curkart->f68 = 0;
        } else
            curkart->f68 = 1;
    }
    x -= spr_h >> 1;
    if (curkart->f44 == 0) {
        if (v88c42) {
            spr_y = v88c1a;
            if ((spr_w - 1) + spr_y > 0xc7) {
                n = spr_w - 1 + spr_y - 0xc7;
                spr_w -= n;
                v7d878 = n * 4;
            }
        } else if (curkart->f26 == 0 && curkart->f56 == 5) {
            v7d878 = 0x10;
            spr_w -= 4;
            spr_y += 4;
        }
    }
    p7d86c += 0x28;
    CR->f1c = 0;
    CR->f24 = 0;
    CR->f10 = 0x7c;
    CR->f18 = v88b7e;
    CR->f04 = x;
    CR->f06 = spr_y;
    CR->f00 = spr_h;
    CR->f02 = spr_w;
    CR->f08 = p7d838;
    CR->f0c = (int)sprptr;
    v88b78++;
    CR->f14 = v7d878;
}

/* ---- 0x26ec4, 37 bytes: palette 8-bit to 6-bit, 0x300 bytes in place (callers 0x18537, 0x194e7,
 * 0x27c46; inlined in dat_pal and file_pal). */
void pal_6bit(unsigned char *p)
{
    short i;

    for (i = 1; i <= 0x300; i++) {
        *p = (short)((short)*p >> 1) >> 1;
        p++;
    }
}

/* ---- 0x26eec, 118 bytes, no callers (dead code): the last 0x300 bytes of the WACKY.DAT entry name
 * (a PCX palette) into buf, made 6-bit. The game inlines seekdat(telldat() + datsize() - 0x300);
 * the two temps reproduce the inlined calls' order (the since2 pattern of car_laps). */
void dat_pal(char *name, unsigned char *buf)
{
    long t, n;

    datfind(name);
    t = ftell(datfile);
    n = datdir[datidx].size;
    fseek(datfile, t + n - 0x300, SEEK_SET);
    fread(buf, 0x300, 1, datfile);
    pal_6bit(buf);
}

/* ---- 0x27028, 56 bytes: the length of an open file, keeping its position (inlined in file_pal). */
long flength(FILE *f)
{
    long cur, n;

    cur = ftell(f);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, cur, SEEK_SET);
    return n;
}

/* ---- 0x26f64, 140 bytes: the palette at the end of the PCX file name into buf, made 6-bit
 * (caller 0x349cf in 0x348f4). */
void file_pal(char *name, unsigned char *buf)
{
    FILE *f;

    f = fopen(name, "rb");
    fseek(f, flength(f) - 0x300, SEEK_SET);
    fread(buf, 0x300, 1, f);
    fclose(f);
    pal_6bit(buf);
}

/* ---- 0x26ff0, 56 bytes: reads a CR-terminated line from f into s (the LF after it is read over),
 * NUL-terminated (callers 0x27205, 0x27215, 0x27225 in 0x271ce). */
void read_line(char *s, FILE *f)
{
    for (;;) {
        fread(s, 1, 1, f);
        if (*s == '\r')
            break;
        s++;
    }
    fread(s, 1, 1, f);
    *s = 0;
}

/* ---- 0x27060, 124 bytes: the picture data of the WACKY.DAT entry name (after the 128-byte PCX
 * header, without the 0x300-byte palette) into the screen buffer p6a978[n] (callers frame_draw
 * 0x133fe, 0x17dee, 0x18625, the menus).
 * differs (near-miss, 33 vs 33 insns, n and size swap ESI/EDI): tried declaration orders, five
 * parameter-name pairs, short n, the seek as one expression, a pointer temp. size - 0x380 up front
 * (the game's `sub edi,0x380` before the header read) is right. 24 more spellings (iter 18,
 * .work/probe_pcxload/{P,Q}.C): both locals' types (int/long/unsigned/long cast), separate, last
 * and split declarations, register, n copied into a local, a pointer temp at the definition and at
 * the use, a struct-pointer and a datidx temp, two-statement size, the sub at the use, whence 0,
 * unsigned and long n, and the global externs declared in the other order - all give the same five
 * rows, so the ESI/EDI pairing is a 9.5b tie-break (GOTCHAS "coin flip"), not a missing stmt. */
extern char *p6a978s[];
#pragma aux p6a978s "_p6a978"

void pcx_load(char *name, int n)
{
    char hdr[200];
    long t, size;

    datfind(name);
    t = ftell(datfile);
    size = datdir[datidx].size - 0x380;
    fseek(datfile, t, SEEK_SET);
    fread(hdr, 0x80, 1, datfile);
    fread(p6a978s[n], size, 1, datfile);
}

/* ---- 0x270dc, 160 bytes: the same from the PCX file name; 0 when it does not open or its data
 * is over 64000 bytes, else 1 (caller 0x349b7 in 0x348f4). */
int pcxfile_load(char *name, int n)
{
    char hdr[200];
    FILE *f;
    long size;

    f = fopen(name, "rb");
    if (f == NULL)
        return 0;
    size = flength(f);
    size -= 0x380;
    fread(hdr, 0x80, 1, f);
    if (size > 0xfa00) {
        fclose(f);
        return 0;
    }
    fread(p6a978s[n], size, 1, f);
    fclose(f);
    return 1;
}

/* ---- 0x2717c, 806 bytes: reads the track's .GAM text from WACKY.DAT (caller 0x27c01 in 0x279e8):
 * the four 14-byte names gam7eabc[0..1] and gam7eaa0[0..1] (each given an "A_" prefix), the name
 * gam6a4e0 (BLANK.PCX in a split/two-player game with v88bde), the start point view_x/view_y
 * (+0x400), a skipped line, the eight start-order pairs order_a/order_b (+0x3e0) and, when v88b60,
 * the three values v88b5a, v88b4e, v88b4c. str7d784 = the first name. */
extern char gam7eabc[2][14], gam7eaa0[2][14], gam6a4e0[], str7d784[];
#pragma aux gam7eabc "_arr7eabc"
#pragma aux gam7eaa0 "_arr7eaa0"
#pragma aux gam6a4e0 "_arr6a4e0"
extern short v88b60, v88b5a, v88b4e, v88b4c;

void gam_load(void)
{
    char buf[80];
    char tmp[16];
    short i, j;
    char *p;

    path_join(".GAM");
    v88bd6 = datfind(str7d7b8);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(str7d7b8);
    read_line(gam7eabc[0], datfile);
    read_line(gam7eabc[1], datfile);
    read_line(gam7eaa0[0], datfile);
    read_line(gam7eaa0[1], datfile);
    read_line(gam6a4e0, datfile);
    read_line(buf, datfile);
    for (i = 0; i <= 1; i++) {
        strcpy(tmp, "A_");
        p = gam7eabc[i];
        strcat(tmp, p);
        strcpy(p, tmp);
        strcpy(tmp, "A_");
        p = gam7eaa0[i];
        strcat(tmp, p);
        strcpy(p, tmp);
    }
    view_x = atoi(buf) + 0x400;
    read_line(buf, datfile);
    view_y = atoi(buf) + 0x400;
    read_line(buf, datfile);
    for (j = 0; j <= 7; j++) {
        read_line(buf, datfile);
        order_a[j] = atoi(buf);
        order_a[j] += 0x3e0;
        read_line(buf, datfile);
        order_b[j] = atoi(buf);
        order_b[j] += 0x3e0;
    }
    read_line(buf, datfile);
    v88b60 = atoi(buf);
    if (v88b60) {
        read_line(buf, datfile);
        v88b5a = atoi(buf);
        read_line(buf, datfile);
        v88b4e = atoi(buf);
        read_line(buf, datfile);
        v88b4c = atoi(buf);
        v88b60 = 1;
    }
    strcpy(str7d784, gam7eabc[0]);
    if ((twoplayer || splitscreen) && v88bde)
        strcpy(gam6a4e0, "BLANK.PCX");
}

/* ---- 0x274a4, 168 bytes: fills tb6bd00[0..0xfff], the screen row of the ground at distance i:
 * height h = 0x4650 / i rounded, clamped to 0x15..0xf0, row 0x78 - h/2 + h (caller 0x3677a). */
void ytab_init(void)
{
    int h, r, i, y;

    h = 0xf0;
    y = 0x78 - (short)h / 2;
    tb6bd00[0] = y + h;
    for (i = 1; i < 0x1000; i++) {
        h = 0x4650 / i;
        r = 0x4650 - h * i;
        if (r > i / 2)
            h++;
        if (h < 0x15)
            h = 0x15;
        if (h > 0xf0)
            h = 0xf0;
        y = 0x78 - (short)h / 2;
        tb6bd00[i] = y + h;
    }
}

/* ---- 0x2754c, 620 bytes: loads the surface animations of this track from EFFECTS.SP (caller
 * 0x17d6f in 0x17928): for surfaces 0..v88b7c whose k1/k2 byte for the track is 1, the frames
 * (f108 * f112 resp. f114 * f11e bytes at offset f100/f104) go to v7d80c (from v7d820) unless a
 * surface before it uses the same offset, which then shares the frames (f126/f12a). */
extern char *v7d80c, *v7d820;
extern short v88b7c;

/* This site inlines the real seekdat/readdat (defined early); undo the call-site aliases here. */
#undef seekdat
#undef readdat
void effects_load(void)
{
    int ptr1[42], offs1[42], offs2[42], ptr2[42];
    long base;
    short n1, n2;
    short i, j, found;
    int size;

    for (i = 0; i <= 0x28; i++) {
        offs1[i] = -1;
        offs2[i] = -1;
    }
    v7d80c = v7d820;
    n1 = 0;
    n2 = 0;
    datfind("EFFECTS.SP");
    base = ftell(datfile);
    for (i = 0; i <= v88b7c; i++) {
        if (surfs[i].k1[track - 1] == 1) {
            found = 0;
            for (j = 0; j <= 0x28; j++)
                if (offs1[j] == surfs[i].f100) {
                    found = j + 1;
                    break;
                }
            if (found == 0) {
                size = surfs[i].f108 * surfs[i].f112;
                seekdat(base + surfs[i].f100);
                readdat(v7d80c, size);
                surfs[i].f126 = (int)v7d80c;
                offs1[n1] = surfs[i].f100;
                ptr1[n1++] = (int)v7d80c;
                v7d80c += size;
            } else
                surfs[i].f126 = ptr1[found - 1];
        }
        if (surfs[i].k2[track - 1] == 1) {
            found = 0;
            for (j = 0; j <= 0x28; j++)
                if (offs2[j] == surfs[i].f104) {
                    found = j + 1;
                    break;
                }
            if (found == 0) {
                size = surfs[i].f114 * surfs[i].f11e;
                seekdat(base + surfs[i].f104);
                readdat(v7d80c, size);
                surfs[i].f12a = (int)v7d80c;
                offs2[n2] = surfs[i].f104;
                ptr2[n2++] = (int)v7d80c;
                v7d80c += size;
            } else
                surfs[i].f12a = ptr2[found - 1];
        }
    }
}
#define seekdat seekdat_x
#define readdat readdat_x

/* ---- 0x277b8, 266 bytes: loads the track's pictures (caller 0x27f18 in 0x279e8): for each of the
 * two gam7eabc / gam7eaa0 names, pcx_load into screen 2 and the asm 0x376d7 copies it (p6a978 ->
 * v7d99c, p6a980 -> p7d714) to v7eb58, which steps through v7d75c by a quarter of v7d604, the second
 * set starting half of v7d604 further on. */
extern char *v7eb58, *v7d99c, *v7d75c;
extern int v7d604;
void pcx_tiles(void);

void track_pics_load(void)
{
    short i;
    char *c, *a, *b;
    char tmp[16];
    int q, d;

    q = v7d604 / 4;
    d = q + q;
    c = v7d75c;
    a = c;
    b = c + d;
    for (i = 0; i <= 1; i++) {
        strcpy(tmp, gam7eabc[i]);
        pcx_load(tmp, 2);
        v7eb58 = a;
        p7d714 = p6a980;
        v7d99c = p6a978;
        pcx_tiles();
        strcpy(tmp, gam7eaa0[i]);
        pcx_load(tmp, 2);
        v7d99c = p6a978;
        v7eb58 = b;
        p7d714 = p6a980;
        pcx_tiles();
        if (i != 1) {
            a += q;
            b += q;
        }
    }
}

/* ---- 0x278c4, 91 bytes: waits until no input is held - keyboard (key_select sets joy88b40) and the four
 * joy88b4x flags, polled through buttons_poll; with flag, the abort key (key88cd0 == 1) ends it early
 * (callers 0x18767, 0x18e99, 0x195c0, 0x1c7d9, 0x1cc2a). */
void wait_release(short flag)
{
    for (;;) {
        if (flag && key88cd0 == 1)
            return;
        v7d8a4 = v7d8ac;
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
        if (joy88b40 == 0 && joy88b42 == 0 && joy88b44 == 0 && joy88b46 == 0)
            return;
    }
}

/* ---- 0x27920, 200 bytes: waits for an input; 1 for the abort key (with mode), else 0 on any key
 * or joy88b4x flag or, in mode 2, after 0x440 ticks; the screenshot key is served while waiting
 * (callers 0x17f24, 0x17fa0, 0x1c7ea, 0x1c801, 0x1da77). */
int wait_input(short mode)
{
    joy88b40 = 0;
    joy88b42 = 0;
    joy88b44 = 0;
    joy88b46 = 0;
    if (mode == 2)
        ticks = 0;
    for (;;) {
        v7d8a4 = v7d8ac;
        buttons_poll();
        if (mode == 2 && ticks >= 0x440)
            return 0;
        if (mode && key88cd0 == 1)
            return 1;
        if (key_select)
            return 0;
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        if (joy88b40 || joy88b42 || joy88b44 || joy88b46)
            return 0;
    }
}

/* ---- 0x279e8, 1614 bytes: loads track `track` (caller 0x17d6a in 0x17928): its picture set from
 * 0x80ace (0xf3c bytes per v88b88; 2 while v7ec80, then 5) unless v88ca0/v88bde; the names
 * SHRINK<n>.PCX, <n>.SPW (entname), <n>.PAR, <n>.RD; gam_load; the palette of str7d784 into p7d718;
 * <n>.POS: the 0x2000-byte map (64x64 shorts, zone classes, map_at src/ww.c:1513) and ncells
 * (its max; v88ba0 = ncells / 4); <n>.SIN: 0x6c 12-byte records at 0x7ad88 - the first dword
 * is read and dropped, the kept pair is the surface class of that tile with mask byte 0 /
 * nonzero, read back by surface_at 0x37b80-0x37ba9 (findings/formats.md); screen 1 from gam6a4e0 unless v7d998 is 1 or 2; tilemap_fill;
 * <n>.M: rows 0x20..0x5f of tilemap (v7d648 dwords each), 0x40 tile pointers v7d75c + b * 0x400 from
 * offset 0x80, b = 1..107 (tile 32x32, mask half at v7d75c + 0x1b000); track_pics_load; ents_load; <n>.RD: v88b7a + 1 lines of 14 bytes, each a segment
 * (+0x400) whose points line_points appends at linep (trklines[i]: points, count, f0a).
 * Most DAT reads here are the inlined seekdat/readdat/telldat (the _in copies), the palette ones
 * real calls, pal_6bit a call (alias).
 * Two shapes this function needed for its bytes (iter 29): the .SIN loop as a do/while whose end
 * pointer comes from the loop variable (`end = sp + 0x6c` -> `lea edi,[esi+0x360]` 0x27d80; the `for`
 * with the folded constant compares against an immediate, a `while` adds a second pre-test), which
 * also hoists the fread count (`mov ebp,1` 0x27d86, `mov ebx,ebp` 0x27d9a/0x27dae); and the 0x80ace
 * seek written on its own local (`m = v88b88 - 1; m *= 0xf3c; seekdat_in(t + m);`) so that the
 * sign-extend lands in ECX and the decrement is `dec ecx` (0x27a49/0x27a50) instead of 9.5b's
 * `movsx eax` + `lea ecx,[eax - 1]` for the one-expression form. */
struct rdrec { short x0, y0, x1, y1, f08, f0a, f0c; };
extern struct rdrec rd7ee0c;
#pragma aux rd7ee0c "_arr7ee0c"
extern char str7eb6c[], str6a4b8[], sin7ad88[0x6c][8];
#pragma aux str7eb6c "_arr7eb6c"
#pragma aux str6a4b8 "_arr6a4b8"
#pragma aux sin7ad88 "_arr7ad88"
extern char *v7d870, *tiles_base, *tilemap;
extern int v7d648, *v7d688;
extern short *v7ed14, v88ba0;
void tilemap_fill(void);

/* The DAT helpers as the game inlines them at some sites (the seekdat/readdat/telldat macros
 * above name non-inlinable aliases); pal_6bit called, not inlined. */
static void seekdat_in(long ofs)
{
    fseek(datfile, ofs, SEEK_SET);
}
static long telldat_in(void)
{
    return ftell(datfile);
}
static void readdat_in(void *buf, int n)
{
    fread(buf, n, 1, datfile);
}
void pal_6bit_x(unsigned char *p);
#pragma aux pal_6bit_x "pal_6bit_"

void track_load(void)
{
    int m;
    char name[20];
    int skip;
    unsigned char c;
    char *pal;
    char (*sp)[8], (*end)[8];
    int row, j;
    long t;
    short i;

    itoa(track, str7d770, 10);
    if (v88ca0 && v88bde == 0) {
        datfind((char *)lit80ace);
        t = telldat_in();
        if (v7ec80)
            v88b88 = 2;
        m = v88b88 - 1;
        m *= 0xf3c;
        seekdat_in(t + m);
        readdat_in(img7eb5c, 0xf3c);
        if (v7ec80)
            v88b88 = 5;
    }
    strcpy(str7eb6c, "SHRINK");
    strcat(str7eb6c, str7d770);
    strcat(str7eb6c, ".PCX");
    strcpy(entname, str7d770);
    strcat(entname, ".SPW");
    strcpy(str6a4b8, str7d770);
    strcat(str6a4b8, ".PAR");
    strcpy(name, str7d770);
    strcat(name, ".RD");
    gam_load();
    pal = p7d718;
    datfind(str7d784);
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    path_join(".POS");
    v88bd6 = datfind(str7d7b8);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(str7d7b8);
    readdat_in(map, 0x2000);
    readdat_in(&ncells, 2);
    v88ba0 = ncells / 4;
    path_join(".SIN");
    v88bd6 = datfind(str7d7b8);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(str7d7b8);
    sp = sin7ad88;
    end = sp + 0x6c;              /* the game's `lea edi,[esi+0x360]`, 0x27d80 */
    do {
        readdat_in(&skip, 4);
        readdat_in(sp, 8);
        sp++;
    } while (sp != end);
    v7d870 = (char *)sin7ad88;
    if (v7d998 == 0 || v7d998 == 3)
        pcx_load(gam6a4e0, 1);
    tiles_base = v7d75c;
    tilemap_fill();
    path_join(".M");
    v88bd6 = datfind(str7d7b8);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(str7d7b8);
    for (row = 0x20; row != 0x60; row++) {
        v7d688 = (int *)tilemap + v7d648 * row;
        v7d688 += 0x20;
        for (j = 1; j <= 0x40; j++) {
            readdat_in(&c, 1);
            *v7d688++ = (int)v7d75c + (c << 10);
        }
    }
    track_pics_load();
    ents_load();
    v88bd6 = datfind(name);
    if (v88bd6 == 0 && v88bea != 0)
        logerr(name);
    readdat_in(&v88b7a, 2);
    linep = v7ed14;
    v88b7a--;
    for (i = 0; i <= v88b7a; i++) {
        readdat_in(&rd7ee0c, 0xe);
        lx0 = rd7ee0c.x0 + 0x400;
        lx1 = rd7ee0c.x1 + 0x400;
        ly0 = rd7ee0c.y0 + 0x400;
        ly1 = rd7ee0c.y1 + 0x400;
        if (i == 0) {
            v88c06 = ly0;
            v88c04 = lx0;
        }
        trklines[i].f06 = rd7ee0c.f0a;
        trklines[i].pts = linep;
        trklines[i].n = line_points();
    }
}

/* ---- 0x28038, 328 bytes: loads the kart sprite sets into the stream at v7d844 (callers 0x28399,
 * 0x3491a, 0x34e95): with `one`, ACTION.SP (0x5e2 bytes into p6a980) and the single set 251X6.INF;
 * else the six sets named in kartset[]. Each set is 10 frames: w, h (shorts) then (h + 1) * w * 4
 * bytes; tab6e470[set * 20 + frame] points at each.
 * differs (26 rows, iter 36: 48 -> 26): only stack slots left (the game has its three loop temps -
 * name pointer, row offset, n * 0x50 - at esp+0/4/8, `one` at 0xc, w/h at 0x10/0x14; ours another
 * permutation, the dead-end class), one entry load order (0x2806a) and where `p7d838 += 2` lands.
 * What fixed the rest: datfind in an if/else instead of a ternary argument (9.5b then strength-
 * reduces k away like the game, 48 -> 36), p7d838 += sz through an int alias (`add [p7d838],eax`,
 * 31) and the table store written before `p = p7d838` (load into EDX, copy to ESI, 26). Tried:
 * a 1-D `tab6e470[k * 20 + i]` index (steps by 0x14, not 0x50), `n > k`, w/h declared first or
 * after p, `p = kartfr[k][i] = p7d838`, `int wh[2]` (24 rows but at esp+0, not the game's 0x10). */
struct kartset { int f00; char name[0x12]; };
extern struct kartset kartsets[];
#pragma aux kartsets "_arr7d8b4"
extern short *v7d844;
extern short *kartfr[][20];
#pragma aux kartfr "_tab6e470"

extern int a7d838x;                   /* p7d838 as an int: `add [p7d838],eax` (0x2814f) */
#pragma aux a7d838x "_p7d838"
void kartsets_load(int one)
{
    int n, k;
    short *p;
    int w, h;
    short i, sz;

    p7d838 = v7d844;
    if (one) {
        n = 1;
        datfind("ACTION.SP");
        fread(p6a980, 0x5e2, 1, datfile);
    } else
        n = 6;
    for (k = 0; k < n; k++) {
        if (0 != one)
            datfind("251X6.INF");
        else
            datfind(kartsets[k].name);
        for (i = 0; i <= 9; i++) {
            kartfr[k][i] = p7d838;
            p = p7d838;
            fread(&w, 2, 1, datfile);
            *p++ = w;
            fread(&h, 2, 1, datfile);
            *p = h;
            p7d838 += 2;
            sz = (h + 1) * w * 4;
            fread(p7d838, sz, 1, datfile);
            a7d838x += sz;
        }
    }
}

/* ---- 0x28180, 546 bytes: loads the game-wide data once (caller 0x36770 in 0x35ca4): MAP.XY (two
 * 0x800 tables at 0x802cc, 0x7faca); WACKY.SDX (v88b7c surface records of 0x12e bytes into
 * surfs[], v88b7c left one less); carves the v7d750 block into v7d6a0 (TRIG.DAT, v7d640 bytes),
 * v7d660 (NDIST, v7d658), tilemap, v7d844, v7d75c (sizes v7d658, v7d600, v7d628), with v7d69c =
 * v7d6a0 + 4 (written as a += 4, read, -= 4) and v7d698 = v7d6a0 + 0x3bf8; CARS.SP (0x18f00 bytes at v7d708, v7d6c8 its end);
 * GIG.MOV (v88b80, then v88b80 * 4 + 4 bytes at 0x7b548); then kartsets_load(1). */
extern char arr802cc[], arr7faca[], arr7b548[];
#pragma aux arr802cc "_arr802cc"
#pragma aux arr7faca "_arr7faca"
#pragma aux arr7b548 "_arr7b548"
extern char *v7d660;
extern int v7d640, v7d658, v7d600, v7d628;
/* The block carving is done on the pointers as ints: char * arithmetic gets scheduled differently. */
extern int a6a0, a660, a6a4, a844, a75c, a69c, a698, a750;
#pragma aux a6a0 "_v7d6a0"
#pragma aux a660 "_v7d660"
#pragma aux a6a4 "_v7d6a4"
#pragma aux a844 "_v7d844"
#pragma aux a75c "_v7d75c"
#pragma aux a69c "_v7d69c"
#pragma aux a698 "_v7d698"
#pragma aux a750 "_v7d750"
extern short v88b80;

void data_load(void)
{
    datfind("MAP.XY");
    fread(arr802cc, 0x800, 1, datfile);
    fread(arr7faca, 0x800, 1, datfile);
    v88bd6 = datfind("WACKY.SDX");
    if (v88bd6 == 0 && v88bea != 0)
        logerr("WACKY.SDX");
    fread(&v88b7c, 2, 1, datfile);
    fread(surfs, v88b7c * 0x12e, 1, datfile);
    a75c = (a844 = (a6a4 = (a660 = (a6a0 = a750) + v7d640) + v7d658) + v7d600) + v7d628;
    a69c = (a6a0 += 4);
    a6a0 -= 4;
    v88b7c--;
    a698 = a6a0;
    a698 += 0x3bf8;
    v88bd6 = datfind("TRIG.DAT");
    if (v88bd6 == 0 && v88bea != 0)
        logerr("TRIG.DAT");
    readdat_in(v7d6a0, v7d640);
    v88bd6 = datfind("NDIST");
    if (v88bd6 == 0 && v88bea != 0)
        logerr("NDIST");
    readdat_in(v7d660, v7d658);
    v7d6c8 = (int)(v7d708 + 0x18f00);
    datfind("CARS.SP");
    readdat_in(v7d708, 0x18f00);
    datfind("GIG.MOV");
    fread(&v88b80, 2, 1, datfile);
    fread(arr7b548, v88b80 * 4 + 4, 1, datfile);
    kartsets_load(1);
}

/* ---- 0x283a4, 168 bytes: the track pairs around the heading (callers in kart_move): v7d678's
 * pair -> (v7d614, v7d5e4); the one 0x12 steps behind curpos (wrapping at 0x780) -> (v7d618, v7d5e8),
 * its index v7d5cc; the one 0x12 ahead -> (v7d61c, v7d5ec), index v7d5c8; v7d690 left at the last
 * pair's second word. Same bytes as `(int *)(v7d6a0 + i * 8)`, but the int-pointer form puts the
 * curpos load after the first pair's stores, as the game does (0x283ca), and EBX/ECX the right way
 * round: 31 rows -> MATCH (iter 32). */
extern int v7d5c8, v7d5cc;

void track_around(void)
{
    v7d690 = (int *)v7d6a0 + v7d678 * 2;
    v7d614 = *v7d690++;
    v7d5e4 = *v7d690;
    v7d5cc = curpos - 0x12;
    if (v7d5cc < 0)
        v7d5cc += 0x780;
    v7d690 = (int *)v7d6a0 + v7d5cc * 2;
    v7d618 = *v7d690++;
    v7d5e8 = *v7d690;
    v7d5c8 = curpos + 0x12;
    if (v7d5c8 >= 0x780)
        v7d5c8 -= 0x780;
    v7d690 = (int *)v7d6a0 + v7d5c8 * 2;
    v7d61c = *v7d690++;
    v7d5ec = *v7d690;
}

/* ---- 0x2844c, 52 bytes: curpos's track pair -> (vec_x, vec_y), v7d690 pointing at it (caller
 * 0x305cd in 0x2fd8c). Index first and the int alias of v7d6a0: the char * form loads v7d6a0 first. */
void track_here(void)
{
    v7d690 = (int *)(curpos * 8 + a6a0);
    vec_x = *v7d690++;
    vec_y = *v7d690--;
}

/* ---- 0x28480, 488 bytes: the view box around the kart (callers 0x134ac, 0x135a4, 0x2de14,
 * 0x2f9b4): the track points 0xa0 steps behind (index v7d674) and ahead (v7d670), each scaled by
 * 0x462 through vec_scale and offset by the kart (view_x, view_y); the box (v88c16, v88c18) -
 * (v88c2a, v88c2c) holds both and the kart. Then curpos's pair into vec_x/vec_y and v7d614/v7d5e4. */
void view_box(void)
{
    short x1, x2, y1, y2;               /* this order (one of five of 24) gives the registers */

    v7d674 = curpos - 0xa0;
    if (v7d674 < 0)
        v7d674 += 0x780;
    v7d690 = (int *)(v7d6a0 + v7d674 * 8);
    vec_x = *v7d690++;
    vec_k = 0x462;
    vec_y = *v7d690;
    vec_scale();
    x1 = view_x + vec_kx;
    y1 = view_y + vec_ky;
    v7d670 = curpos + 0xa0;
    if (v7d670 >= 0x780)
        v7d670 -= 0x780;
    v7d690 = (int *)(v7d6a0 + v7d670 * 8);
    vec_x = *v7d690++;
    vec_k = 0x462;
    vec_y = *v7d690;
    vec_scale();
    x2 = view_x + vec_kx;
    y2 = view_y + vec_ky;
    v88c18 = y1;
    if (y1 > view_y)
        v88c18 = view_y;
    if (y2 < v88c18)
        v88c18 = y2;
    v88c16 = x1;
    if (x1 > view_x)
        v88c16 = view_x;
    if (x2 < v88c16)
        v88c16 = x2;
    v88c2c = y1;
    if (y1 < view_y)
        v88c2c = view_y;
    if (y2 > v88c2c)
        v88c2c = y2;
    v88c2a = x1;
    if (x1 < view_x)
        v88c2a = view_x;
    if (x2 > v88c2a)
        v88c2a = x2;
    v7d690 = (int *)(v7d6a0 + curpos * 8);
    vec_x = *v7d690++;
    vec_y = *v7d690--;
    v7d614 = vec_x;
    v7d5e4 = vec_y;
}

/* ---- 0x28668, 259 bytes: steps the two colour cycles while v88b60 runs (callers 0x2f7a0,
 * 0x308e8): every v88b4c frames v88b38 (0..3) picks v7d6d0 + 12 * it for dac_put8; v88b3a (0..8)
 * picks v7d758 + 15 * it for dac_put5 every v88b5a ticks of v88b60, but at 3 it waits v88b4e and
 * jumps to 4. */
extern short v88b38, v88b3a, v88b50;
extern char *dac_src, *v7d758, *v7d6d0;
void dac_put8(void);
void dac_put5(void);

void cycles_step(void)
{
    if (v88b60 == 0)
        return;
    if (v88b50 >= v88b4c) {
        if (++v88b38 > 3)
            v88b38 = 0;
        dac_src = v7d6d0 + v88b38 * 12;
        dac_put8();
        v88b50 = 0;
    }
    if (v88b3a == 3 && v88b60 >= v88b4e) {
        v88b3a = 4;
        v88b60 = 0;
        dac_src = v7d758 + 0x3c;
        dac_put5();
    } else if (v88b60 >= v88b5a && v88b3a != 3) {
        if (++v88b3a > 8)
            v88b3a = 0;
        v88b60 = 0;
        dac_src = v7d758 + v88b3a * 15;
        dac_put5();
    }
    v88b60++;
    v88b50++;
}

/* ---- 0x2876c, 128 bytes: v7d800's direction frame from its track position (caller 0x288bc in
 * 0x287ec): v88c0e = pos / 0xf0, frame = v88c0e & 7, moved one on (wrapping 0..7) in the second
 * half of a 0xf0 step - up when pos > v88c0e, else down; f1a = dirfr7d328[frame]. */
extern short v88c0e, dirfr7d328[];
#pragma aux dirfr7d328 "_arr7d328"

void kart_dirframe(void)
{
    int pos;
    short a, r;

    pos = v7d800->pos;
    v88c0e = pos / 0xf0;
    r = pos % 0xf0;
    a = v88c0e & 7;
    if (r >= 0x78) {
        if (pos > v88c0e) {
            if (++a > 7)
                a = 0;
        } else if (--a < 0)
            a = 7;
    }
    v7d800->f1a = dirfr7d328[a];
}

/* ---- 0x287ec, 426 bytes: each player's sprite frame as seen by viewer v88b7e (callers 0x134dc,
 * 0x135e5, 0x289b4): v88c30 is the viewer's direction class from curpos (as kart_dirframe); per
 * player (v7d800, v88b72; in two-player or the first two of split screen kart_dirframe first)
 * f38[viewer] = dirtab706a0[v88c30][f1a]; with f28[viewer] clear, f30[viewer] is set for frames
 * 0/2/4/6 of the other karts in a net game, else for frame 4 with f5c clear. */
extern short v88c30, dirtab706a0[][8];
#pragma aux dirtab706a0 "_arr706a0"

void players_frames(void)
{
    short r, v;

    v88c0e = curpos / 0xf0;
    r = curpos % 0xf0;
    v88c30 = v88c0e & 7;
    if (r >= 0x78) {
        if (v88c0e < curpos) {
            if (++v88c30 > 7)
                v88c30 = 0;
        } else if (--v88c30 < 0)
            v88c30 = 7;
    }
    v7d800 = players;
    for (v88b72 = 0; v88b72 <= nplayers; v88b72++, v7d800++) {
        if (twoplayer || (splitscreen && v88b72 < 2))
            kart_dirframe();
        v = v7d800->f38[v88b7e] = dirtab706a0[v88c30][v7d800->f1a];
        if (v7d800->f28[v88b7e] == 0) {
            if (netvoices && v88b72 > 0) {
                v7d800->f30[v88b7e] = 0;
                if (v == 0 || v == 2 || v == 4 || v == 6)
                    v7d800->f30[v88b7e] = 1;
            } else if (v7d800->f38[v88b7e] == 4 && v7d800->f5c == 0)
                v7d800->f30[v88b7e] = 1;
            else
                v7d800->f30[v88b7e] = 0;
        }
    }
}

/* ---- 0x28998, 84 bytes: builds the frame's sprite list for viewer v88b7e: clears v88bf4 and the
 * count v88b78, starts the records at arr79a28, sets the player frames, projects players, ents,
 * the own kart, the start lights (while v88c38) and shots, and draws the list if it has any. */
void sprites_build(void)
{
    v88bf4 = 0;
    v88b78 = 0;
    p7d86c = arr79a28;
    players_frames();
    players_project();
    ents_project();
    self_project();
    if (v88c38)
        lights_project();
    shots_project();
    if (v88b78)
        sprites_draw();
}

/* ---- 0x289ec, 289 bytes: v7d800's hit timer (caller 0x28ce7 in 0x28cac). The limit is 0x21 ticks
 * for f58 1, else 0x32 (4 in a net game). At 0x32 the hit voice alternates: f58 2 <-> 3 with
 * voices 0x19 / 0x18 when the voice channel is free (the first voice_check gets 0x19 for 2, 0x18
 * for 3, the uninitialised value otherwise). When ticks2 - f4c reaches the limit f58 is cleared;
 * in a net game voice 0xd plays, f56/f58/f5c clear, f02 = 2 and v7edb4 rises by 2 for the kart
 * whose face is v7ec6c, else by 1, capped at 0x63. */
extern int i7ec6c, v7edb4;
#pragma aux i7ec6c "_v7ec6c"

void hit_timer(void)
{
    int lim, v, m;

    if (v7d800->f58 == 1)
        lim = 0x21;
    else
        lim = 0x32;
    if (netvoices)
        lim = 4;
    if (lim == 0x32) {
        switch (v7d800->f58) {
        case 2:
            v = 0x19;
            break;
        case 3:
            v = 0x18;
            break;
        }
        voice_check(v);
        if (voicebusy == 0) {
            if (v7d800->f58 == 2)
                v7d800->f58 = 3;
            else
                v7d800->f58 = 2;
            v = (v7d800->f58 == 2) + 0x18;
            voice_check(v);
            if (voicebusy == 0)
                voice_play(v);
        }
    }
    if ((m = v7d800->f4c, ticks2 - m) >= lim) {   /* the inlined since2() shape */
        v7d800->f58 = 0;
        if (netvoices) {
            voice_play(0xd);
            v7d800->f56 = 0;
            v7d800->f58 = 0;
            v7d800->f5c = 0;
            v7d800->f02 = 2;
            if (v7d800->face == i7ec6c)
                v7edb4 += 2;
            else
                v7edb4++;
            if (v7edb4 > 0x63)
                v7edb4 = 0x63;
        }
    }
}

/* ---- 0x28b20, 380 bytes: catch-up for the computer kart in place n (caller 0x28da9 in 0x28cac),
 * single-player races only (not split screen, net voices or v7edb0). From place 4 on, while
 * v7d800->f04 is not 0 or 1: a kart more than 7 - n + v7ece0 behind karts[0] (f2e) goes to state
 * 3 with f0e 0x7d00 and speed f0a = tbl7e730[v88b84] + 0x10/0x1c/0x24/0x28 for places 4..7; a
 * close one leaves state 3 for 2. From place 5 on, curkart->fac 1..3 adds to v88c24.
 * Written on v88c24 directly: the game holds it in CX for the whole function (and our build does
 * the same); d is an int (movsx load).
 * differs (near-miss, 1 real block): after `f0a = d` the game reloads `mov cx,[ebx+0xa]` for
 * v88c24, ours copies EDX. Tried: a local c (6 declaration orders, register), short d, chained
 * assignment, volatile read, a local struct pointer, a different lvalue for the read; iter 31
 * `(volatile short *)&v7d800->f0a` on the read - 9.5b still forwards the word store into
 * `mov ecx,edx` (GOTCHAS). The other 20 fndiff rows are 3 masked-fixup `[ebx-@]/[ebx+@]` fakes
 * and 17 alignment-epilogue rows (ours 396 bytes vs the game 380, LAYOUT-class). */
extern short v88c24;

void kart_catchup(int n)
{
    int gap, d;

    if (!splitscreen && !netvoices && !v7edb0) {
        gap = karts[0].f2e - karts[n].f2e;
        if (n >= 4 && v7d800->f04 != 1 && 0 != v7d800->f04) {
            if (7 - n + v7ece0 <= gap) {
                if (v7d800->f04 != 3) {
                    d = tbl7e730[v88b84];
                    switch (n) {
                    case 7:
                        d += 0x28;
                        break;
                    case 6:
                        d += 0x24;
                        break;
                    case 5:
                        d += 0x1c;
                        break;
                    case 4:
                        d += 0x10;
                        break;
                    }
                    v7d800->f04 = 3;
                    v7d800->f0a = d;
                    v7d800->f0e = 0x7d00;
                    v88c24 = v7d800->f0a;
                }
            } else if (v7d800->f04 == 3)
                v7d800->f04 = 2;
            if (n >= 5 && curkart->fac <= 3) {
                switch (curkart->fac) {
                case 1:
                    if (n == 7)
                        v88c24 += 5;
                    if (n == 6)
                        v88c24 += 4;
                    if (n == 5)
                        v88c24 += 3;
                    break;
                case 2:
                    if (n == 7)
                        v88c24 = v88c24 + 1;
                    if (n == 6)
                        v88c24 += 2;
                    if (n == 5)
                        v88c24 += 3;
                    break;
                case 3:
                    if (n == 6)
                        v88c24++;
                    if (n == 5)
                        v88c24 += 2;
                    break;
                }
            }
        }
    }
}

/* ---- 0x28cac, 954 bytes: drives the computer karts for one frame (callers 0x3277c, 0x3278d in
 * 0x30aae). Per player (v7d800, v88b72; not the human ones: 0, and 1 in two-player or split
 * screen; f58 set runs hit_timer instead; net games skip f02 == 2): the f04 state sets the step
 * count v88c24 - 0: f0c steps for f06 frames, then 1; 1: f0a steps for f0e frames, then 2; 2: f08
 * steps and kart_catchup; 3: f0a steps until f0e runs out or the gap to karts[0] closes, then 2.
 * A spin (f00, 3 = the other way) pushes x by f14, which falls by 4 a frame. In state 2 a kart
 * within 0x14 (x + y distance) of another adds 0x3c steps; v7edb0 without v88c3c takes 0xc off
 * (at least 4). Each step reads the next (x, y) of the track line at f5e (x + the push), moving to
 * the next trklines[] entry (f1c, wrapping after v88b7a) when f10 runs out, and checks the human
 * karts: within 0xe sets f02 = 1 and that kart's f7c. */
extern short v88c3c;
extern struct trkline *v7d7f8;

void cpu_drive(void)
{
    short dir, x, step, y, k;           /* this order (of 120) gives the game's registers */
    int lim, gap, j;

    v7d800 = players;
    for (v88b72 = 0; v88b72 <= nplayers; v88b72++) {
        if (v7d800->f58)
            hit_timer();
        else {
            if (netvoices && v7d800->f02 == 2) {
                v7d800++;
                continue;
            }
            if ((!splitscreen && !twoplayer || v88b72 != 1) && v88b72 != 0) {
                switch (v7d800->f04) {
                case 0:
                    v88c24 = v7d800->f0c;
                    if (--v7d800->f06 <= 0)
                        v7d800->f04 = 1;
                    break;
                case 1:
                    v88c24 = v7d800->f0a;
                    if (--v7d800->f0e <= 0)
                        v7d800->f04 = 2;
                    break;
                case 2:
                    v88c24 = v7d800->f08;
                    kart_catchup(v88b72);
                    break;
                case 3:
                    gap = karts[0].f2e - karts[v88b72].f2e;
                    v88c24 = v7d800->f0a;
                    if (--v7d800->f0e <= 0 || gap <= 7 - v88b72 + v7ece0)
                        v7d800->f04 = 2;
                    break;
                }
                dir = 0;
                if (v7d800->f00) {
                    if (v7d800->f00 == 3)
                        dir = -v7d800->f14;
                    else
                        dir = v7d800->f14;
                    v7d800->f14 -= 4;
                    if (v7d800->f14 <= 0)
                        v7d800->f00 = 0;
                }
                if (v7d800->f00 == 0 && v7d800->f04 == 2 && !netvoices) {
                    x = v7d800->x;
                    y = v7d800->y;
                    for (k = 7; k >= 1; k--) {
                        if (k == v88b72)
                            continue;
                        if (abs(players[k].x - x) + abs(players[k].y - y) < 0x14) {
                            v88c24 += 0x3c;
                            break;
                        }
                    }
                }
                if (v7edb0 && v88c3c == 0) {
                    v88c24 -= 0xc;
                    if (v88c24 <= 4)
                        v88c24 = 4;
                }
                for (step = 1; step <= v88c24; step++) {
                    v7d800->x = *(short *)v7d800->f5e;
                    if (dir)
                        v7d800->x += dir;
                    v7d800->f5e += 2;
                    v7d800->y = *(short *)v7d800->f5e;
                    v7d800->f5e += 2;
                    if (--v7d800->f10 == 0) {
                        if (++v7d800->f1c > v88b7a)
                            v7d800->f1c = 0;
                        v7d7f8 = &trklines[v7d800->f1c];
                        v7d800->f1a = v7d7f8->f06;
                        v7d800->f5e = (int)v7d7f8->pts;
                        v7d800->f10 = v7d7f8->n;
                    }
                    if (twoplayer || splitscreen)
                        lim = 1;
                    else
                        lim = 0;
                    for (j = 0; j <= lim; j++) {
                        if (karts[j].f0a == 0 && karts[j].f7e == 0 && v88c3c == 0
                            && abs(v7d800->x - players[j].x) + abs(v7d800->y - players[j].y) < 0xe) {
                            v7d800->f02 = 1;
                            karts[j].f7c = 1;
                        }
                    }
                }
            }
        }
        v7d800++;
    }
}

/* ---- 0x290a0, 3436 bytes: the human kart's frame (curkart; callers 0x325b7, 0x326cf in 0x30aae).
 * v88c42 1: the kart is pushed 0x40 back along the track pair at curpos, kept in 0x400..0xbfe,
 * and (v88b68, v88b6a) is 0x7c further. v88c42 0: while knocked out (f0a) the hit animation
 * (f18 0xe, f1a 0..7, tb7b448/tb7b518) and f0c count down; otherwise the steer keys (key88cdd
 * right -> v88b98, key88cd5 left -> v88b8c, swapped while f14) pick the lean animation f1c, joy88b40
 * accelerates (f42 up to 0x64, +6 in two-player/net, 0x36 under v7edb0), joy88b44 brakes (f42 - 8,
 * voice 0x11) and its hold drives the dope animation, joy88b46 hops (f06, voice 0x11), f42 maps to
 * the speed f26 through tbl7e730; then kart_state, kart_move and the boost zone (surf_class 2:
 * in_zone, voice 0xc, f42 0xa0). Every frame f18 > 0 (one tick since f32) steps animation f18:
 * 1/2/14 the hit frames, 3/4 the hop, 5 lean f1a toward f1c, 6/7 tb7b418 frames with a 3-tick hold
 * into 8/9 (the stamp is the dword at +2), 10 the win voice and tb7b378, 13 the tb7b238 loop.
 * Animation resets go to f18 0, f1a 2, f1c 2, f00 0 with tb7b518.
 * differs (near-miss, 535 rows): the game keeps the global curkart in EDX through the whole function
 * (store before each call and exit, reload after), GOTCHAS "promotes a global"; ours reads it from
 * memory at every use (744 vs 651 insns, 36 blocks, aligned block for block after the v88c42 != 0
 * branch was put first). Cases ending in return instead of break: no change. Iter 44: the shots_project
 * local-pointer shape - `struct kart *k = curkart;` at entry, `k->` at all 139 sites (the ANIM_RESET macro
 * included), `curkart = k;` before the twelve calls and five returns and `k = curkart;` after them, each
 * group braced so no brace-less if body changes binding - gains 2 rows (535 -> 533) either order, and
 * nmrefs still differs on counts: the game makes 19 more 0x7d5b0(curkart) references than even that, so
 * 9.5b is caching the global itself in EDX and no C spelling of a local pointer reproduces it. Reverted. */
extern short v7e85c, v88b92, v88b90, v88c9c, v88c5c, v88c3a, v88c5a, v88c36;
extern int kd7b4d8[][2], kd7b378[], kd7b238[][2];
#pragma aux kd7b4d8 "_tb7b4d8"
#pragma aux kd7b378 "_arr7b378"
#pragma aux kd7b238 "_arr7b238"
extern int i7d610, i7d620;
#pragma aux i7d610 "_v7d610"
#pragma aux i7d620 "_v7d620"

#define ANIM_RESET() { curkart->f1a = 2; curkart->f00 = 0; curkart->f18 = 0; curkart->f1c = 2; \
                       curkart->f4e = &tb7b518[curkart->f1a][v88b7e]; }

void kart_frame(void)
{
    v88b92 = 0;
    v88b90 = 0;
    if (v88c42) {
        if (v88c42 == 1) {
            v7d690 = (int *)(v7d6a0 + curpos * 8);
            vec_k = 0x40;
            vec_x = *v7d690;
            v7d690++;
            vec_y = *v7d690;
            v7d690--;
            vec_scale();
            i7d610 = -i7d610;
            view_x += vec_kx;
            i7d620 = -i7d620;
            view_y += vec_ky;
            if (view_x > 0xbfe)
                view_x = 0xbfe;
            if (view_y > 0xbfe)
                view_y = 0xbfe;
            if (view_x < 0x400)
                view_x = 0x400;
            if (view_y < 0x400)
                view_y = 0x400;
            vec_k = 0x7c;
            vec_scale();
            v88b68 = view_x + vec_kx;
            v88b6a = view_y + vec_ky;
        }
    } else {
        if (curkart->f0a) {
            curkart->f26 = v7e85c;
            if (curkart->f0c > 4 && curkart->f0c <= 6 && curkart->f18 != 0xe) {
                curkart->f1a = 0;
                curkart->f18 = 0xe;
                curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                curkart->f32 = ticks2;
            }
            if (curkart->f18 == 0xe) {
                if (++curkart->f1a >= 8)
                    ANIM_RESET()
                else
                    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
            }
            v88b8c = 0;
            v88b98 = 0;
            if (--curkart->f0c <= 4)
                curkart->f0a = 2;
        } else {
            if (key88cdd == 0 && key88cd5 == 0 && (curkart->f18 == 0 || curkart->f18 == 5)
                && curkart->f1a != 2) {
                curkart->f18 = 5;
                curkart->f1c = 2;
            }
            v88b98 = key88cdd != 0;
            v88b8c = key88cd5 != 0;
            if (curkart->f14) {
                if (v88b8c) {
                    v88b8c = 0;
                    v88b98 = 1;
                } else if (v88b98) {
                    v88b8c = 1;
                    v88b98 = 0;
                }
            }
            if (curkart->f0a) {
                v88b8c = 0;
                v88b98 = 0;
            }
            if (v7d998) {
                if (v88b98 && curkart->f26 == 0) {
                    v88b98 = 0;
                    if (curkart->f18 == 0 || curkart->f18 == 5) {
                        curkart->f1c = 3;
                        curkart->f18 = 5;
                    }
                }
                if (v88b8c && curkart->f26 == 0) {
                    v88b8c = 0;
                    if (curkart->f18 == 0 || curkart->f18 == 5) {
                        curkart->f1c = 1;
                        curkart->f18 = 5;
                    }
                }
            }
            if (v88b8c) {
                if (curkart->f26 == 0 && v88c38)
                    anitimer++;
                else
                    anitimer = 0;
                curkart->f24 = 0;
                curkart->f22++;
                if (curkart->f18 == 0 || curkart->f18 == 5) {
                    curkart->f1c = 0;
                    curkart->f18 = 5;
                }
            } else {
                anitimer = 0;
                curkart->f22 = 0;
            }
            if (v88b98) {
                curkart->f22 = 0;
                curkart->f24++;
                if (curkart->f18 == 0 || curkart->f18 == 5) {
                    curkart->f1c = 4;
                    curkart->f18 = 5;
                }
            } else
                curkart->f24 = 0;
            if (joy88b40) {
                anitimer = 0;
                if (curkart->fb4)
                    joy88b40 = 0;
            }
            if (!twoplayer && !splitscreen && !netvoices) {
                if (joy88b44 && v88c38) {
                    if (anikick == 0 && anitimer >= 0x36) {
                        anikick = 1;
                        ani_dope();
                        anitimer = 0;
                    }
                } else
                    anitimer = 0;
            }
            if (joy88b44 && curkart->f42 > 0) {
                joy88b40 = 0;
                voice_check(0x11);
                if (voicebusy == 0)
                    voice_play(0x11);
                curkart->f42 -= 8;
                if (curkart->f42 < 0)
                    curkart->f42 = 0;
                curkart->f26 = tbl7e730[curkart->f42];
            }
            if (joy88b40 == 0 && players[v88b7e].f62 > 2)
                players[v88b7e].f62 = 0;
            if (v7edb0 && v88c3c == 0)
                joy88b40 = 1;
            if (joy88b40 || curkart->fb4) {
                if (curkart->fb4 == 0) {
                    if (v7edb0)
                        curkart->f42 = 0x36;
                    else if (twoplayer || netvoices)
                        curkart->f42 += 6;
                    else
                        curkart->f42 += 0xa;
                    if (curkart->f42 > 0x64)
                        curkart->f42 = 0x64;
                    if (players[v88b7e].f62 == 0) {
                        players[v88b7e].f62 = 1;
                        players[v88b7e].f54 = 0;
                    }
                } else if (--curkart->fb6 <= 0)
                    curkart->fb4 = 0;
            } else {
                curkart->f42 -= 4;
                if (curkart->f42 < 0)
                    curkart->f42 = 0;
            }
            curkart->f26 = tbl7e730[curkart->f42];
            if (curkart->f0a == 0 && curkart->f3c == 0 && curkart->f20 == 0 && curkart->f42
                && joy88b46 && curkart->f06 == 0) {
                voice_play(0x11);
                curkart->f08 = 1;
                curkart->f06 = (v88b98 != 0) + 1;
                curkart->f32 = ticks2;
                if (curkart->f06 == 1) {
                    curkart->f3c = 1;
                    curkart->f18 = 3;
                    curkart->f1a = 0;
                    curkart->f4e = &kd7b4d8[0][v88b7e];
                } else {
                    curkart->f1a = 6;
                    curkart->f18 = 4;
                    curkart->f3c = 2;
                    curkart->f4e = &kd7b4d8[6][v88b7e];
                }
            }
            if (curkart->f0a == 0 && surf_class != 0xa)
                kart_state();
        }
        v88b94 = curkart->f26;
        v7d678 = curpos;
        kart_move();
        if (surf_class == 2 && curkart->fb4 == 0) {
            curkart->fb4 = 1;
            curkart->fb4 = in_zone();
            if (curkart->fb4) {
                voice_play(0xc);
                curkart->f42 = 0xa0;
                curkart->fb6 = 0x10;
                curkart->f26 = tbl7e730[curkart->f42];
            }
        } else if (curkart->f42 <= 0)
            curkart->fb4 = 0;
        if (curkart->f0a)
            return;
        if (v88c3c)
            curkart->f18 = v88c36;
        if ((curkart->f18 && since2(curkart->f32) >= 1) || curkart->f18 == 5) {
            curkart->f32 = ticks2;
            switch (curkart->f18) {
            case 1:
                if (curkart->f20)
                    return;
                if (--curkart->f1a > 6)
                    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                else
                    ANIM_RESET()
                break;
            case 2:
                if (curkart->f20)
                    return;
                if (++curkart->f1a < 6)
                    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                else
                    ANIM_RESET()
                break;
            case 3:
                if (curkart->f3c)
                    return;
                if (++curkart->f1a < 3)
                    curkart->f4e = &kd7b4d8[curkart->f1a][v88b7e];
                else
                    ANIM_RESET()
                break;
            case 4:
                if (curkart->f3c)
                    return;
                if (--curkart->f1a > 3)
                    curkart->f4e = &kd7b4d8[curkart->f1a][v88b7e];
                else
                    ANIM_RESET()
                break;
            case 6:
                if (curkart->f1a == 0) {
                    if (ticks2 - *(int *)&curkart->f02 >= 3)
                        curkart->f18 = 8;
                } else if (--curkart->f1a == 0)
                    *(int *)&curkart->f02 = ticks2;
                curkart->f4e = &tb7b418[curkart->f1a][v88b7e];
                break;
            case 7:
                if (curkart->f1a == 4) {
                    if (ticks2 - *(int *)&curkart->f02 >= 3)
                        curkart->f18 = 9;
                } else if (++curkart->f1a == 4)
                    *(int *)&curkart->f02 = ticks2;
                curkart->f4e = &tb7b418[curkart->f1a][v88b7e];
                break;
            case 8:
                if (++curkart->f1a < 2)
                    curkart->f4e = &tb7b418[curkart->f1a][v88b7e];
                else {
                    curkart->f00 = 0;
                    curkart->f18 = 0;
                    curkart->f1a = 2;
                    curkart->f1c = 2;
                    curkart->f4e = &tb7b518[curkart->f1a][v88b7e];
                }
                break;
            case 9:
                if (--curkart->f1a > 2)
                    curkart->f4e = &tb7b418[curkart->f1a][v88b7e];
                else {
                    curkart->f00 = 0;
                    curkart->f18 = 0;
                    curkart->f1a = 2;
                    curkart->f1c = 2;
                    curkart->f4e = &tb7b518[curkart->f1a][v88b7e];
                }
                break;
            case 5:
                if (curkart->f1a < curkart->f1c)
                    curkart->f1a++;
                else if (curkart->f1a > curkart->f1c)
                    curkart->f1a--;
                if (curkart->f1a == curkart->f1c)
                    curkart->f18 = 0;
                curkart->f4e = &tb7b518[curkart->f1a][v88b7e];
                break;
            case 10:
                if (v88c9c <= 3) {
                    voice_check(myplayer);
                    if (voicebusy == 0) {
                        voice_play(myplayer);
                        v88c9c++;
                    }
                }
                if (++v88c5c == v88c52) {
                    v88c5c = 5;
                    v88c3a++;
                }
                curkart->f4e = &kd7b378[v88c5c];
                break;
            case 13:
                if (++v88c5a == v88c50) {
                    v88c5a = 0;
                    v88c3a++;
                }
                curkart->f4e = kd7b238[v88c5a];
                break;
            case 14:
                if (++curkart->f1a < 8)
                    curkart->f4e = &tb7b448[curkart->f1a][v88b7e];
                else
                    ANIM_RESET()
                break;
            }
        }
    }
}

/* ---- 0x29e0c, 1114 bytes: the race HUD (callers 0x13684, 0x314f2, 0x3195d), not in split
 * screen without v88bde, a v7d998 mode other than 0/3, or a net game. One player: the lives
 * digit karts[v7eb80 ? 0 : 1].f6c at (0x76, 0xb); two players: both scores (v684e0/v685e4, the own
 * one first by netgame) as two zero-padded digits from the font at 0x6e380 by character. With
 * v88bde: the two 'SCORE' panels (box_draw) and in the full screen both scores at (0x107, 0x64)
 * and (0x107, 0). Then, in a one-player race with no lives left (curkart->f6c), GAME OVER and
 * the v7c650 tune to its end (or 500 ticks without music), then the menu song. */
extern char *font6e380[];
#pragma aux font6e380 "_arr6e380"
extern int v7c650;
void spr_draw_key(void);
void music_start_c(int song, int loop);         /* music_start, called (not inlined) */
#pragma aux music_start_c "music_start_"

#define PAD2(b) { if ((b)[1] == 0) { c = (b)[0]; (b)[2] = 0; (b)[0] = '0'; (b)[1] = c; } }

void hud_draw(void)
{
    char buf[4];
    char c;

    spr_h = 0xa;
    spr_w = 9;
    if (splitscreen && v88bde == 0)
        return;
    if (v7d998 != 0 && v7d998 != 3)
        return;
    if (netvoices)
        return;
    if (v88bde == 0) {
        if (twoplayer == 0) {
            spr_x = 0x76;
            spr_y = 0xb;
            sprptr = sp6e440[v7eb80 ? (int)karts[0].f6c : (int)karts[1].f6c];
            spr_draw_opaque();
        } else {
            ink = '.';
            if (netgame == 1)
                itoa(v684e0, buf, 10);
            else
                itoa(v685e4, buf, 10);
            spr_x = 0x76;
            spr_y = 0xb;
            PAD2(buf)
            spr_h = 0xa;
            sprptr = font6e380[(unsigned char)buf[0]];
            spr_w = 9;
            spr_draw_key();
            spr_x += 0xa;
            sprptr = font6e380[(unsigned char)buf[1]];
            spr_draw_key();
            if (netgame == 1)
                itoa(v685e4, buf, 10);
            else
                itoa(v684e0, buf, 10);
            spr_x = 0x76;
            spr_y = v7ec7c;
            PAD2(buf)
            sprptr = font6e380[(unsigned char)buf[0]];
            spr_draw_key();
            spr_x += 0xa;
            sprptr = font6e380[(unsigned char)buf[1]];
            spr_draw_key();
        }
    } else if (splitscreen) {
        border = '.';
        ink = '.';
        spr_y = 0x65;
        spr_x = 0xdd;
        spr_h = 0x26;
        spr_w = 8;
        box_draw();
        spr_x = 0xdd;
        spr_y = 1;
        box_draw();
    } else {
        border = '.';
        ink = '.';
        spr_y = 0x65;
        spr_x = 0xdd;
        spr_h = 0x26;
        spr_w = 8;
        box_draw();
        print_at(spr_x, spr_y, "SCORE", 0);
        spr_h = 0x26;
        spr_w = 8;
        spr_x = 0xdd;
        spr_y = 1;
        box_draw();
        print_at(spr_x, spr_y, "SCORE", 0);
        itoa(v684e0, buf, 10);
        PAD2(buf)
        spr_h = 0xa;
        spr_w = 9;
        spr_y = 0x64;
        spr_x = 0x107;
        sprptr = font6e380[(unsigned char)buf[0]];
        spr_draw_opaque();
        spr_x += 0xa;
        sprptr = font6e380[(unsigned char)buf[1]];
        spr_draw_opaque();
        spr_x -= 0xa;
        spr_y -= 0x64;
        itoa(v685e4, buf, 10);
        PAD2(buf)
        sprptr = font6e380[(unsigned char)buf[0]];
        spr_draw_opaque();
        spr_x += 0xa;
        sprptr = font6e380[(unsigned char)buf[1]];
        spr_draw_opaque();
        spr_x -= 0xa;
        spr_y -= 0x64;
    }
    if (!twoplayer && !splitscreen && curkart->f6c == 0) {
        fn_17888("GAME OVER", 0);
        if (musicon) {
            music_start_c(v7c650, 0);
            while (MUSIC_SongPlaying())
                ;
            music_start_c(menusong, 1);
        } else {
            ticks = 0;
            ticks2 = 0;
            while (ticks < 0x1f4)
                ;
        }
    }
}

/* ---- 0x2a268, 425 bytes: shows netstats[1..n] in a centred box (the width from netstats[1]) on
 * the dimmed palette (page_to 0xbb80, page_draw = page_from = page_shown; restored after), then with `wait`
 * waits for a key (v88b9e = wait_input(1)) or else until ticks passes t (callers 0x113f3,
 * 0x18de3 and more). Defined under its own name: the callers' prototype carries a pragma for
 * the ECX pass-through.
 * Iter 73: 18 -> 13 rows and the game's 428 bytes with `for (i = 1; i <= n; i++, y += 9)
 * print_at(x, y, netstats[i], 0)` - an index loop, not the line pointer: 9.5b folds a pointer's
 * `&netstats[1]` into one immediate, an index's strength-reduced pointer starts as `mov esi,0x3c` +
 * `add esi,netstats` like the game (probe: the shape needs an int index, GOTCHAS dead-end entry
 * rewritten). Left: the x/y vs n/wait slot order and the two `mov ax` loads (`int x, y` fixes the
 * loads but gives y a register: 24 rows).
 * differs (before iter 73, near-miss, 18 rows, 103 vs 104 insns, 10 blocks): the game keeps t in EBP, the line pointer in
 * ESI and i in EDI, with x/y and n/wait in stack slots (x at [esp], y [esp+4]); ours spills t and
 * orders the slots the other way. Tried: all 120 declaration orders, int x/y, an indexed loop.
 * 18 fndiff rows and 10 of them are only the slot displacements (same mnemonic), so the rest cannot
 * buy a MATCH: 2 rows are 9.5b folding `&netstats[1]` into one relocated immediate where the game
 * has `mov esi,0x3c` + `add esi,0x7c818` (0x2a359/0x2a35d), 2 are our `mov ax,word[spr_x]`/
 * `mov ax,word[spr_y]` (0x2a331/0x2a340) vs the game's `movsx eax`, 2 the tail filler.
 * Iter 65 behaviour pass (globrefs census): 0 bugs, 1 same-shape - that fold is confirmed at linked
 * level, our two fixups resolve to 0x7c854, exactly the game's `mov edi,0x7c854` 0x2a297, and both
 * sides step the record table with `add esi,0x3c` (0x19f70 / 0x2a37b), so netstats[0..n] are read in
 * the same order; nothing else in the function touches an address one side lacks. The other 2 rows,
 * our `mov ax,[spr_y]` 0x19f29 / `mov ax,[spr_x]` 0x19f38 against the game's `movsx eax`
 * 0x2a332/0x2a340, are also same-shape behaviourally: both sides re-read those slots with
 * `movsx word` on the way into print_at (ours 0x19f57/0x19f5c, the game 0x2a363/0x2a368), so the
 * high half our 16-bit store leaves dirty is never read.
 * Iter 47 (.work/probe_cl, control 16 there): an `int tv` temp (`tv = spr_y + 4; y = tv;`) scored
 * 15 in the probe but 18 -> 20 in src/ww.c (rejected, GOTCHAS "Rank in a probe, decide in
 * src/ww.c"); `(int)` casts on the two sums, `p = netstats[1];` before the for, and
 * `&netstats[1][0]` all keep the probe control's 16. */
void showlines_def(int n, int wait, int t);
#pragma aux showlines_def "showlines_"

void showlines_def(int n, int wait, int t)
{
    short h, x, y;
    int i;

    save_a = page_draw;
    save_b = page_from;
    save_c = page_to;
    page_to = 0xbb80;
    page_draw = page_from = page_shown;
    page_copy();
    h = strlen(netstats[1]) * 8 + 8;
    spr_w = n * 9 + 8;
    spr_x = (0x140 - h) / 2;
    spr_y = (0xc8 - spr_w) / 2;
    ink = 0x2c;
    border = 0x26;
    spr_h = h;
    box_draw();
    y = spr_y + 4;
    x = spr_x + 4;
    for (i = 1; i <= n; i++, y += 9)   /* index, not a pointer: the game's `mov esi,0x3c; add esi,netstats` */
        print_at(x, y, netstats[i], 0);
    if (wait) {
        wait_release(0);
        v88b9e = wait_input(1);
        wait_release(0);
    } else {
        ticks = 0;
        while (ticks <= t)
            ;
    }
    page_from = 0xbb80;
    page_draw = page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
}

/* ---- 0x2a414, 152 bytes: one line in a centred box, 0x10 high, at row y (0x5c when 0). */
void msgbox(char *s, int y)
{
    short h;
    int row;

    h = strlen(s) * 8 + 8;
    spr_x = (0x140 - h) / 2;
    spr_w = 0x10;
    spr_h = h;
    if ((short)y == 0) {
        row = 0x5c;
        spr_y = row;
    } else
        spr_y = y;
    ink = 0x2c;
    border = 0x26;
    box_draw();
    print_at((short)(spr_x + 4), (short)(spr_y + 4), s, 0);
}

/* ---- 0x2a4ac, 364 bytes: the minimap (caller 0x2a935 in 0x2a92c), while v88ca0: the map image
 * img7eb3c at (0, 0x3c), 0x32 x 0x4e, then a dot per player (pixel_plot) at the track position mapped
 * through the 0x800-entry tables MAP.XY (arr802cc for x, arr7faca for y, +0x3c), x/y - 0x400
 * clamped to 0..0x7ff. Colour 0xff for the own player (1 for netgame 2), 0x54 for the other one
 * in a net game with more than two players, else 0x5a.
 * MATCH (iter 36): spr_x and spr_y are the clamp variables themselves; the game caches spr_y in
 * ESI across the loop because it is the working variable (no local copies). */
void pixel_plot(void);

void minimap(void)
{
    int me, i;

    if (v88ca0 == 0)
        return;
    spr_x = 0;
    spr_y = 0x3c;
    spr_h = 0x4e;
    spr_w = 0x32;
    sprptr = img7eb3c;
    spr_draw_opaque();
    me = 0;
    if (netgame && netgame != 1)
        me = 1;
    for (i = 0; i <= nplayers; i++) {
        if (i == me)
            ink = 0xff;
        else
            ink = 0x5a;
        if (netgame && nplayers > 1) {
            if (me == 0 && i == 1)
                ink = 0x54;
            if (me == 1 && i == 0)
                ink = 0x54;
        }
        spr_x = players[i].x - 0x400;
        spr_y = players[i].y - 0x400;
        if (spr_x < 0)
            spr_x = 0;
        else if (spr_x > 0x7ff)
            spr_x = 0x7ff;
        if (spr_y < 0)
            spr_y = 0;
        else if (spr_y > 0x7ff)
            spr_y = 0x7ff;
        spr_x = (unsigned char)arr802cc[spr_x];
        spr_y = (unsigned char)arr7faca[spr_y] + 0x3c;
        pixel_plot();
    }
}

/* ---- 0x2a618, 224 bytes: formats a race time t (tenths) as "MM:SS:T" into out (callers 0x2a9a4,
 * 0x2aa19 in 0x2a92c); a time over 0x8ca0 (one hour) shows as 0 and clears racetime.
 * differs (near-miss, 31 rows, registers and slots only): the game keeps t in EBP and the divisor
 * 10 in EBX, ours t in EBX and 10 in EBP, and the two int locals land in [esp+4]/[esp+0xc] where the
 * game has [esp+0xc]/[esp+8]. Tried: 24 declaration orders, a separate t/10 local, nested divisions,
 * long t, statement orders, buf sizes. New here (kept, 33 -> 31): `char buf[4]` - the largest itoa
 * writes is "599"+NUL - and the frame becomes the game's 0x10 instead of 0x14. SKIP at 1 variant:
 * the rest is the documented register-home / slot dead ends (GOTCHAS "Dead ends", QUESTIONS 1/2/9/10). */
void time_str(int t, char *out)
{
    char buf[4];                   /* 0x8ca0/60 = 599 is the largest itoa writes here: 3 + NUL */
    int sec, tenths, min;

    if (t > 0x8ca0) {
        t = 0;
        racetime = 0;
    }
    tenths = t % 10;
    t /= 10;
    min = t / 60;
    sec = t % 60;
    strcpy(out, "00:00:0");
    itoa(min, buf, 10);
    if (buf[1] == 0) {
        buf[1] = buf[0];
        buf[0] = '0';
    }
    out[0] = buf[0];
    out[1] = buf[1];
    itoa(sec, buf, 10);
    if (buf[1] == 0) {
        buf[1] = buf[0];
        buf[0] = '0';
    }
    out[3] = buf[0];
    out[4] = buf[1];
    itoa(tenths, buf, 10);
    out[6] = buf[0];
}

/* ---- 0x2a6f8, 70 bytes: draws the 7 characters of s at (x, y) with the 7 x 9 font at 0x7d618
 * (indexed by character; callers 0x2a9c9, 0x2a9e4). */
extern char *font7d618[];
#pragma aux font7d618 "_arr7d618"

void time_draw(short x, short y, char *s)
{
    int i;

    spr_h = 7;
    spr_w = 9;
    spr_y = y;
    for (i = 0; i < 7; i++) {
        spr_x = x;
        sprptr = font7d618[(unsigned char)s[i]];
        spr_draw();
        x += 7;
    }
}

/* ---- 0x2a740, 491 bytes: the lap / finish banner for the two players (caller 0x2b15a in
 * 0x2a92c), outside v7d998 modes. While cars[i].fd4 (the banner, 1-based into sp7b210) runs: the
 * frame fe4 (0..3) of sp7b210[fd4 - 1] (0x688 bytes each), or with fe0 (finished) sp6a538[3 - fe4]
 * moved down 8; shown for split-screen players (the second one p2offset higher) or the viewers in
 * v7eb80; offset by the GIG.MOV path entry fe8 (0x7b548: x + 0xfa, y), which advances until fe0
 * is 1 and after v88b80 restarts with fe0 = 1. n is reused for the 3 - f index and the show flag
 * (one register in the game); 3 - f is written f + 3 - (f + f) for the game's lea/lea/sub. After frame 3 a finished banner ends (fd4/fdc 0, its sound stopped);
 * an ended sound (FX_SoundActive) calls car_finished first. */
struct gigpt { short x, y; };
extern struct gigpt gig7b548[];
#pragma aux gig7b548 "_arr7b548"
extern short viewer7eb80[];
#pragma aux viewer7eb80 "_v7eb80"
int FX_SoundActive(int handle);

void lap_banner(void)
{
    int i, n, f, k;

    if (v7d998)
        return;
    for (i = 0; i <= 1; i++) {
        n = cars[i].fd4;
        if (n == 0)
            continue;
        if (soundon && cars[i].ff0 && !FX_SoundActive(cars[i].fec))
            car_finished(i);
        n--;
        f = cars[i].fe4;
        if (cars[i].fe0)
        {
            n = f + 3 - (f + f);
            sprptr = sp6a538[n];
        }
        else
            sprptr = sp7b210[n] + f * 0x688;
        if (v88bde) {
            n = 1;
            if (i == 0)
                spr_y = 0x78;
            else
                spr_y = 0x78 - (short)p2offset;
        } else {
            spr_y = 0x78;
            if (viewer7eb80[i])
                n = 1;
            else
                n = 0;
        }
        if (n) {
            spr_h = 0x26;
            k = cars[i].fe8;
            if (cars[i].fe0) {
                spr_w = 0x1c;
                spr_y += 8;
            } else
                spr_w = 0x2c;
            spr_x = 0xfa + gig7b548[k].x;
            spr_y += gig7b548[k].y;
            spr_draw();
        }
        if (cars[i].fe0 != 1 && ++cars[i].fe8 > v88b80) {
            cars[i].fe8 = 0;
            cars[i].fe0 = 1;
            cars[i].fe4 = 0;
        }
        if (++f > 3) {
            f = 0;
            if (cars[i].fe0) {
                if (soundon && cars[i].ff0) {
                    FX_StopSound(cars[i].fec);
                    cars[i].ff0 = f;
                }
                cars[i].fd4 = 0;
                cars[i].fdc = 0;
            }
        }
        cars[i].fe4 = f;
    }
}

/* ---- 0x2a92c, 2370 bytes: the race overlay (callers 0x1367f, 0x2fb6e): the minimap; in a
 * one-player race the clock (racetime, v7eccc while v88c3c; reset unless v88c44 is 2) when
 * optclock or v88bb2 4; in a net game the countdown v7ec88 - racetime (v7ec84 set at 0) and the
 * count v7edb4; the v88c48 message; in two-player / split screen the second banner (cars[].fb0,
 * like lap_banner at x 0x8d, but shown where v7eb80 is clear); per player the engine pitch from the speed karts[].f26 and the
 * speedometer needle f28 (shown with optspeed: icon ic7ea84 and two digits of 0x7c118) easing
 * toward 2 * f26; the place icons (0x6e6ec); the pickup counts karts[].f6a; the blinking light
 * f96/f98 (lp6e720/lp6e724); in v7d998 modes 2/3 the ofont2 panel instead of the rest; else
 * lap_banner and LAP n / LAST LAP! / FINISHED! (v88c3c) while f94, unless banner fd4 runs.
 * differs (46 rows; iter 39 62 -> 46): the frame is the game's 0x34 with `buf[0xc]` (the game has buf
 * at [esp+0x14] and its dword slots from 0x20; 0x14 for both arrays gave 0x3c and moved every slot),
 * the pitch is `f26 * 0x14 - 0x708` (the game's `imul; lea edx,[eax-0x708]` 0x2ad2c, size back to
 * 2372), the banner index is written into the sp6a538 subscript (no k), and `up, down` are declared in
 * that order. Left, register / stack-slot choice only: the banner's `f + 3 - (f + f)` (EAX/EBX
 * swapped), the scalar slots (game lim at [esp+0x20], down at 0x30; ours 0x30 and 0x24), the pitch's
 * EAX vs our EDX, and the loop counters (EBP/ESI, EDX/ECX, EBX/ECX in the later loops). */
extern int v7eccc, v7ec88, v7ec84;
extern short v88c48, v88b66;
extern char str6aa18[];
#pragma aux str6aa18 "_arr6aa18"
extern char *font7c118[], *sp6e6ec[];
#pragma aux font7c118 "_arr7c118"
#pragma aux sp6e6ec "_arr6e6ec"
void gig_sound2(int i);
void FX_SetPitch(int handle, int pitchoffset);

void overlay_draw(void)
{
    char str[0x14];
    char buf[0xc];
    int v, n, t, one, s, f, k, i, up, lim, down;

    minimap();
    one = 1;
    if (twoplayer || (v7d998 != 0 && v7d998 != 3) || netvoices)
        one = 0;
    if (one) {
        if (v88c44 != 2)
            racetime = 0;
        t = racetime;
        if (v88c3c)
            t = v7eccc;
        time_str(t, str);
        if (optclock || v88bb2 == 4) {
            time_draw(0x108, 0x70, str);
            if (v88bde)
                time_draw(0x108, 0xc, str);
        }
    }
    if (netvoices) {
        t = racetime;
        t = v7ec88 - t;
        if (t <= 0) {
            v7ec84 = 1;
            t = 0;
        }
        time_str(t, str);
        time_draw(0x108, 0x70, str);
        spr_h = 0xa;
        spr_w = 9;
        itoa(v7edb4, buf, 10);
        if (buf[1] == 0) {
            buf[1] = buf[0];
            buf[2] = 0;
            buf[0] = '0';
        }
        spr_y = 0x20;
        spr_x = 0x1a;
        sprptr = font6e380[(unsigned char)buf[0]];
        spr_draw_opaque();
        spr_x += 0xa;
        sprptr = font6e380[(unsigned char)buf[1]];
        spr_draw_opaque();
        spr_x -= 0xa;
    }
    if (v88c48)
        print_at(v88b66, 0x64, str6aa18, 1);
    if (twoplayer || splitscreen) {
        spr_x = 0x8d;
        for (i = 0; i <= 1; i++) {
            n = cars[i].fb0;
            if (n == 0)
                continue;
            if (soundon && cars[i].fcc && !FX_SoundActive(cars[i].fc8))
                gig_sound2(i);
            --n;
            f = cars[i].fb8;
            if (cars[i].fb4)
                sprptr = sp6a538[f + 3 - (f + f)];
            else
                sprptr = sp7b210[n] + f * 0x688;
            if (v88bde) {
                n = 1;
                if (0 == i)
                    spr_y = 0x78 - p2offset;
                else
                    spr_y = 0x78;
            } else {
                spr_y = 0x78;
                if (!viewer7eb80[i])
                    n = 1;
                else
                    n = 0;
            }
            if (n) {
                spr_h = 0x26;
                k = cars[i].fbc;
                if (cars[i].fb4) {
                    spr_w = 0x1c;
                    spr_y += 8;
                } else
                    spr_w = 0x2c;
                spr_x = 0x8d + gig7b548[k].x;
                spr_y += gig7b548[k].y;
                spr_draw();
            }
            if (cars[i].fb4 != 1 && ++cars[i].fbc > v88b80) {
                cars[i].fbc = 0;
                cars[i].fb4 = 1;
                cars[i].fb8 = 0;
            }
            if (++f > 3) {
                f = 0;
                if (cars[i].fb4) {
                    if (soundon && cars[i].fcc) {
                        FX_StopSound(cars[i].fc8);
                        cars[i].fcc = f;
                    }
                    cars[i].fb0 = 0;
                }
            }
            cars[i].fb8 = f;
        }
    }
    spr_y = 0xb5;
    if (twoplayer || splitscreen)
        lim = 1;
    else
        lim = 0;
    for (i = 0; i <= lim; i++) {
        if (v88c44 == 2) {
            up = 2;
            down = 4;
        } else {
            up = 4;
            down = 0xa;
        }
        if (soundon)
            FX_SetPitch(cars[i].sndhandle, karts[i].f26 * 0x14 - 0x708);
        if (viewer7eb80[i] && !netvoices && !twoplayer && !v7d998 && optspeed) {
            v = karts[i].f28;
            if (v > 0x63)
                v = 0x63;
            if (v88c44 != 2)
                v = 0;
            itoa(v, buf, 10);
            if (i == 1)
                spr_y -= p2offset;
            if (buf[1] == 0) {
                buf[1] = buf[0];
                buf[2] = 0;
                buf[0] = '0';
            }
            spr_x = 0;
            spr_h = 0x2a;
            sprptr = ic7ea84;
            spr_w = 0x13;
            spr_draw_opaque();
            spr_h = 0xa;
            spr_w = 0xf;
            spr_x += 3;
            sprptr = font7c118[(unsigned char)buf[0]];
            spr_y += 2;
            spr_draw_opaque();
            sprptr = font7c118[(unsigned char)buf[1]];
            spr_x += 0xc;
            spr_draw_opaque();
            spr_y -= 2;
        }
        s = karts[i].f26 + karts[i].f26;
        if (karts[i].f28 < s) {
            karts[i].f28 += up;
            if (karts[i].f28 > s)
                karts[i].f28 = s;
        } else if (karts[i].f28 > s) {
            karts[i].f28 -= down;
            if (karts[i].f28 < s)
                karts[i].f28 = s;
        }
    }
    if (one) {
        spr_h = 0x16;
        spr_w = 0x1d;
        spr_x = 0x19;
        if (v88bde)
            spr_y = 0x6f;
        else
            spr_y = 0xc;
        for (i = 0; i <= 1; i++) {
            if (viewer7eb80[i]) {
                sprptr = sp6e6ec[cars[i].place];
                if (i == 1)
                    spr_y -= p2offset;
                if (v88bde) {
                    spr_x = 4;
                    spr_draw();
                } else
                    spr_draw_opaque();
            }
        }
    }
    if (v88bde) {
        spr_x = 0x9a;
        spr_y = 0x64;
    } else {
        spr_x = 0xb5;
        spr_y = 0xb;
    }
    spr_h = 0xa;
    spr_w = 9;
    for (i = 0; i <= 1; i++) {
        if (viewer7eb80[i]) {
            itoa(karts[i].f6a, buf, 10);
            if (buf[1] == 0) {
                buf[2] = 0;
                buf[1] = buf[0];
                buf[0] = '0';
            }
            if (v7d998 == 0 || v7d998 == 3) {
                if (i == 1)
                    spr_y -= p2offset;
                sprptr = font6e380[(unsigned char)buf[0]];
                spr_draw_opaque();
                spr_x += 0xa;
                sprptr = font6e380[(unsigned char)buf[1]];
                spr_draw_opaque();
                spr_x -= 0xa;
            }
        }
    }
    if (twoplayer || netvoices)
        return;
    spr_h = 0x50;
    spr_w = 0x11;
    spr_x = 0x78;
    spr_y = 0xb6;
    for (i = 0; i <= 1; i++) {
        if (cars[i].f96) {
            if (cars[i].f98) {
                sprptr = lp6e724;
                cars[i].f98 = 0;
            } else {
                sprptr = lp6e720;
                cars[i].f98 = 1;
            }
            if (viewer7eb80[i]) {
                if (i == 1)
                    spr_y -= p2offset;
                if (v7d998 == 0)
                    spr_draw();
            }
        }
    }
    if (v7d998 == 2 || v7d998 == 3) {
        spr_y = 0x9b;
        spr_x = 4;
        spr_h = 0x5c;
        sprptr = ofont2;
        spr_w = 0x29;
        spr_draw();
        return;
    }
    lap_banner();
    spr_y = 0xa4;
    for (i = 0; i <= 1; i++) {
        if (cars[i].f94 == 0)
            continue;
        if (cars[i].lap >= nlaps) {
            if (v88c3c)
                strcpy(str, "FINISHED!");
            else
                strcpy(str, "LAST LAP!");
        } else {
            strcpy(str, "LAP ");
            itoa(cars[i].lap, buf, 10);
            strcat(str, buf);
        }
        if (viewer7eb80[i]) {
            spr_x = 4;
            if (1 == i)
                spr_y -= p2offset;
            if (cars[i].fd4 == 0)
                print_at(spr_x, spr_y, str, 0);
        }
    }
}

/* ---- 0x2b270, 744 bytes: the leader board between races (caller 0x2c1fb in 0x2bff0):
 * " LEADER BOARD AFTER " + the race name (0x6aa68) centred at y 4 (11 px per character), two
 * POINTS headers, then places 1..nplayers + 1 from the sorted table 0x799d8 (score, player) in
 * two columns of four, 0x26 apart: the name padded to 8, the two-digit score and the face. The
 * own player's (myplayer) and, in split screen, the second player's (v88c6a) place is returned
 * through the pointers (x, y, player, place) instead of drawing the face, for the caller.
 * differs (73 rows): the game spills x and keeps y in EBP (ours the reverse, so the pointer
 * stores are ordered differently), loads the score with mov ax / cwde, and adds pnames after
 * the imul; tried: ternary / temp for spr_x, int p, (short) casts. Iter 37: spr_x through an
 * `int col` set in each arm (the game's `mov eax,2` / `mov eax,0xaa` + one store) 75 -> 73;
 * col declared first or braced arms change nothing, `short col` 90; permute.py (15 rounds) ranked
 * `*y` before `*x` best in its probe (42 -> 38) but whole-file it is 75 - not taken. */
struct standing { short score, player; int f04; };
extern struct standing standings[];
#pragma aux standings "_arr799d8"
extern char racename[];
#pragma aux racename "_arr6aa68"

void leaderboard(short *x, short *y, short *who, short *place, short *x2, short *y2, short *who2, short *place2)
{
    char name[0x28];
    char buf[0x28];
    short i;
    short p;
    int col;
    int row;
    short draw;

    strcpy(buf, " LEADER BOARD AFTER ");
    strcat(buf, racename);
    strcat(buf, " ");
    wtext((short)((0x140 - (int)strlen(buf) * 0xb) / 2), 4, buf, 0);
    row = 0x32;
    strcpy(buf, "POINTS");
    wtext(0x37, 0x22, buf, 0);
    wtext(0xdf, 0x22, buf, 0);
    for (i = 1; i <= nplayers + 1; i++) {
        p = standings[i].player;
        itoa(standings[i].score, buf, 10);
        if (buf[1] == 0) {
            buf[1] = buf[0];
            buf[2] = 0;
            buf[0] = '0';
        }
        if (i <= 4)
            col = 2;
        else
            col = 0xaa;
        if (i == 5)
            row = 0x32;
        spr_x = col;
        spr_y = row;
        strcpy(name, " ");
        strcat(name, pnames[p]);
        while (strlen(name) < 8)
            strcat(name, " ");
        print_at(spr_x, spr_y, name, 0);
        spr_x += 8;
        print_at(spr_x, spr_y, buf, 0);
        draw = 1;
        spr_x += 8;
        if (p == myplayer) {
            *y = spr_y;
            *x = spr_x;
            *who = p;
            *place = i;
            draw = 0;
        }
        if (splitscreen && p == v88c6a) {
            *x2 = spr_x;
            *y2 = spr_y;
            *who2 = p;
            *place2 = i;
            draw = 0;
        }
        if (draw) {
            spr_h = 0x26;
            sprptr = faces[p].img[0];
            spr_w = 0x1c;
            spr_draw();
        }
        row += 0x26;
    }
}

/* ---- 0x2b558, 143 bytes: n as two digits (leading 0) in the 0x16 x 0x1d font 0x6e350 at
 * (x, y), 0x16 apart; spr_x ends after the second digit (caller 0x2b86c in 0x2b5e8). */
extern char *font6e350[];
#pragma aux font6e350 "_arr6e350"

void bigdigits(int x, int y, int n)
{
    char buf[0xc];

    itoa(n, buf, 10);
    if (buf[1] == 0) {
        buf[1] = buf[0];
        buf[2] = 0;
        buf[0] = '0';
    }
    spr_h = 0x16;
    spr_w = 0x1d;
    spr_x = x;
    spr_y = y;
    sprptr = font6e350[(unsigned char)buf[0]];
    spr_draw();
    sprptr = font6e350[(unsigned char)buf[1]];
    spr_x += 0x16;
    spr_draw();
    spr_x += 0x16;
}

/* ---- 0x2b5e8, 1518 bytes: the Duck Shoot screen on ORDER.PCX (callers 0x17e41, 0x31142).
 * With after set (the shoot is over) the score v7edb4 enters the four-place hall of fame
 * 0x6e768 if it reaches a place (jingle v7c64c, enter_name, the lower places move down) and
 * is shown big; before it the two-line instructions. Then the two drivers' faces (v7ec70
 * facing right, v7ec6c left), THE WACKY DUCK TOP SHOOTERS with the table, after the shoot
 * the race song v7c658 again, PRESS FIRE, fade_in and wait_input. Without after the function
 * returns right after the table (0x2bad6 `je` to the epilogue 0x2bbc9): no PRESS FIRE, no fade,
 * no wait - CONFIRMED by the bytes (the caller does the rest).
 * differs (10 rows, 1520 bytes = the game's length): 8 rows are the masked-fixup artefact
 * ([reg - @] vs [reg + @]); the 2 real ones are the inlined music_start(v7c64c, 0) zero
 * constant that -oe sinks below the stores (game `mov word [0x88bcc],0` 0x2b699 after
 * `xor edx,edx`; GOTCHAS scheduling class, as music_jingle). Fixed this iter (48 -> 10): the
 * three centred x values outside the table loop are an int w (the game's dword slot 0xc8 +
 * movsx reload), the table loop keeps short x; arrays declared buf, name, num (buf at esp+0,
 * num at esp+0x50); `if (!after) return;`; num[1] = num[0] before num[2] = 0. */
struct hiscore { char name[0x14]; int score; };
extern struct hiscore duckfame[4];
#pragma aux duckfame "_arr6e768"
extern int v7c64c;
extern int i7ec70;
#pragma aux i7ec70 "_v7ec70"

void duck_screen(int after)
{
    char buf[0x50];
    char name[0x28];
    char num[0x50];
    int i, j, found, y;
    short x;
    int w;

    pcx_load("ORDER.PCX", 2);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    v88b4a = 0;
    page_draw = 0;
    p7d714 = p6a980;
    pcx_blit();
    page_draw = 0;
    if (after) {
        found = 0;
        for (i = 0; i <= 3; i++) {
            if (v7edb4 >= duckfame[i].score) {
                found = 1;
                break;
            }
        }
        if (found) {
            music_start(v7c64c, 0);
            enter_name(name, "DUCK SHOOT HALL OF FAME", 0xf);
            palptr = pal7d850;
            getpalette();
            fade_out(pal7d850);
            v88b4a = 0;
            page_draw = 0;
            p7d714 = p6a980;
            pcx_blit();
            page_draw = 0;
            for (i = 0; i <= 3; i++) {
                if (v7edb4 >= duckfame[i].score) {
                    if (i != 3) {
                        for (j = 3; j >= i + 1; j--) {
                            duckfame[j].score = duckfame[j - 1].score;
                            strcpy(duckfame[j].name, duckfame[j - 1].name);
                        }
                    }
                    duckfame[i].score = v7edb4;
                    strcpy(duckfame[i].name, name);
                    break;
                }
            }
        }
        strcpy(buf, "YOUR DUCK SCORE");
    } else
        strcpy(buf, "THE WACKY DUCK SHOOT");
    wtext((0x140 - (int)strlen(buf) * 0xb) / 2, 0xa, buf, 0);
    if (after)
        bigdigits(0x8a, 0x3c, v7edb4);
    else {
        strcpy(buf, "YOU HAVE TWO MINUTES TO");
        w = (0x140 - (int)strlen(buf) * 8) / 2;
        print_at(w, 0x1e, buf, 0);
        strcpy(buf, "BLAST AS MANY DUCKS AS YOU CAN!");
        w = (0x140 - (int)strlen(buf) * 8) / 2;
        print_at(w, 0x28, buf, 0);
    }
    spr_y = 0x3c;
    spr_h = 0x26;
    spr_w = 0x1c;
    sprptr = faces[i7ec70].img[7];
    spr_x = 0x4c;
    spr_draw();
    sprptr = faces[i7ec6c].img[1];
    spr_x = 0xcd;
    spr_draw();
    strcpy(buf, "THE WACKY DUCK TOP SHOOTERS");
    wtext((0x140 - (int)strlen(buf) * 0xb) / 2, 0x6e, buf, 0);
    y = 0x82;
    for (i = 0; i <= 3; i++) {
        strcpy(buf, duckfame[i].name);
        strcat(buf, " ");
        itoa(duckfame[i].score, num, 10);
        if (num[1] == 0) {
            num[1] = num[0];
            num[2] = 0;
            num[0] = '0';
        }
        strcat(buf, num);
        x = (0x140 - (int)strlen(buf) * 8) / 2;
        print_at(x, (short)y, buf, 0);
        y += 0xa;
    }
    if (!after)
        return;                         /* 0x2bad6: the intro call ends here - no PRESS FIRE, no fade */
    if (cursong != v7c658)
        music_start(v7c658, 1);
    strcpy(buf, "PRESS FIRE");
    w = (0x140 - (int)strlen(buf) * 8) / 2;
    print_at(w, 0xb4, buf, 0);
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    fade_in((unsigned char *)v7d854 = v7d85c);
    wait_input(0);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    v88b9e = 0;
}

/* ---- 0x2bbd8, 1045 bytes: the two-player overall winner on ORDER.PCX (caller 0x31468 in
 * 0x30aae): the scores cars[0/1].fc0 capped at hits_target; the higher one (myplayer for car 0,
 * v88c6a for car 1) IS THE OVERALL WINNER! with the face, or YOU HAVE BOTH DRAWN! with both.
 * PRESS FIRE (the net second player waits for player one); in a net game the fire is
 * exchanged with net_key (player one sends 1), EXCESSIVE ERRORS! on failure. */
void overall_winner(void)
{
    char buf[0x50];
    int i, w, t;

    if (cursong != v7c658)
        music_start(v7c658, 1);
    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
    pcx_load("ORDER.PCX", 0);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    v88b4a = 0;
    page_draw = 0;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0;
    for (i = 0; i < 2; i++) {
        if (cars[i].fc0 > hits_target)
            cars[i].fc0 = hits_target;
    }
    if (cars[0].fc0 == cars[1].fc0)
        w = 0xff;
    else if (cars[0].fc0 > cars[1].fc0)
        w = myplayer;
    else
        w = v88c6a;
    if (w == 0xff)
        strcpy(buf, "YOU HAVE BOTH DRAWN!");
    else {
        strcpy(buf, pnames[w]);
        strcat(buf, " IS THE OVERALL WINNER!");
    }
    print_at((short)((0x140 - (int)strlen(buf) * 8) / 2), 0x36, buf, 0);
    if (w == 0xff) {
        spr_y = 0x56;
        spr_h = 0x26;
        spr_w = 0x1c;
        sprptr = faces[myplayer].img[0];
        spr_x = 0x78;
        spr_draw();
        sprptr = faces[v88c6a].img[0];
        spr_x += 0x28;
        spr_draw();
    } else {
        spr_y = 0x56;
        spr_h = 0x26;
        spr_w = 0x1c;
        sprptr = faces[w].img[0];
        spr_x = 0x8d;
        spr_draw();
    }
    strcpy(buf, "PRESS FIRE");
    if (netgame && netgame != 1)
        strcpy(buf, "WAITING FOR PLAYER ONE TO PRESS FIRE");
    print_at((short)((0x140 - (int)strlen(buf) * 8) / 2), 0xb4, buf, 0);
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    fade_in((unsigned char *)v7d854 = v7d85c);
    if (netgame) {
        v7ed20 = 0xaa0;
        v7ed1c = 0x2a8;
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
        if (netgame == 1)
            wait_input(0);
        v88cf3 = 0;
        v88cf0 = 0;
        if (netgame == 1)
            v88cf0 = 1;
        net88c14 = net_key(v88cf0, v88cf3, 0);
        t = ticks;
        while (ticks - t < 0x2c)
            ;
        if (net88c14) {
            fn_1c068();
            msgbox("EXCESSIVE ERRORS!", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 0x12c)
                ;
            fn_1c0ac();
        }
    } else
        wait_input(0);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
}

/* ---- 0x2bff0, 4722 bytes: after a race (callers 0x315fa, 0x31e01 in 0x30aae). The points
 * players[].f1e / face go to standings[1..] (0x799d8), sorted by score with rec_swap, and
 * leaderboard draws them on ORDER.PCX; the own (and split-screen second) face then talks
 * (faces[].img[0..7]) until fire (net: exchanged with net_key). After the last race (v88b88 5):
 * the places with equal scores are grouped onto the three rostrum steps (two in a two-player
 * game; a player not on it stands left / right), or with a bonus track won (v88bb2 3, v7ed98)
 * three showlines lines and v7ec80 = 1. Else THE ROSTRUM on WIN.PCX: the karts drop in
 * (sp6a538 frames, phase 1), then stand as faces, the own kart animated (kd7b378) and the side
 * ones (kd7b238), a duck (sp7c800) walks to the winner, and the flags (gn7eb24[g]) rise on
 * each step with voice 0x17; fire ends it (the own winner first goes to scores_screen).
 * differs: 110 = scalar-slot permutation of the 20-odd locals (GOTCHAS dead end; arrays DO
 * match now) + the EAX/ECX sort-pair and EAX/EDX product coin flips + one dead je at the fire
 * test + the trailing lea/nop pad. Frame gap solved this iter: grp[10] and pods[8] (not 9/6) -
 * array rows +0x2c, scalar rows +0x74, padding unchanged (175 -> 110). Tried before: loop forms,
 * a save local, operand order, exit-test shapes. The 1x extra joy88b40 ref in nmrefs predates.
 * iter 64 BEHAV census (globrefs: `counts 0x88b40 joy88b40 ours x12 game x11`, every other address the
 * same count, and its `game only 0x61624/0x6171c x2/0x61730/0x61754/0x61778/0x6179c/0x617a8 x2` are the
 * nine string literals = our nine (WIN.PCX x2, EXCESSIVE ERRORS! x2, THE ROSTRUM, ORDER.PCX, the four
 * win lines)): SAME. Every store matches one for one; the extra use is our third fire test. The game
 * tests joy once at the top of the tail (`cmp [joy],0; je 0x2cb84` 0x2d1e9 - its joy==0 path jumps
 * straight to the loop-continue block 0x2cb84, which our build has instruction for instruction at
 * 0x1c77a: v7e724 = ticks, page_to = page_draw, page_copy, musicon test), then its `if (mypod != 1) break`
 * (`cmp word [esp+0x338],1` 0x2d1f9), then its second joy read + splitscreen (0x2d204/0x2d20e = our
 * 0x1cdf2/0x1cdfc), and for the scores_screen arm only `cmp word [esp+0x338],1; jne 0x2cb84` 0x2d218 -
 * no joy read, joy != 0 is already true there (its `je 0x2d1f7` after the first je and its
 * `jne 0x2d221` are both dead - the documented dead je). Our build does not jump at joy==0 but falls
 * through the two ifs, so `mypod == 1 && joy88b40` must re-read it: ours 0x1ce15 `cmp joy,0; je
 * 0x1c77a` lands on that same continue block and no path with joy == 0 reaches scores_screen on either
 * side. Dropping the `&& joy88b40` to match the count WOULD be a bug: with joy == 0 and mypod == 1 our
 * fall-through would call scores_screen, where the game left the loop at 0x2d1f1. No BUG, no code
 * change. */
struct ptgroup { int n; int pl[10]; };
extern char *flags7eb24[];
extern volatile char vk88cd5, vk88cdd, vk88cd6, vk88cd3;    /* written by the keyboard handler; */
#pragma aux vk88cd5 "_key88cd5"                              /* volatile here keeps the game's */
#pragma aux vk88cdd "_key88cdd"                              /* re-test of netgame after the */
#pragma aux vk88cd6 "_key88cd6"                              /* stores (global volatile loses */
#pragma aux vk88cd3 "_key88cd3"                              /* a match elsewhere) */
#pragma aux flags7eb24 "_gn7eb24"
struct podium { short x, y, n; short pl[10]; short g, gx, gy, gyend, done; };
extern char *v7d858;
extern short v88bec;
void scores_screen(void);

void race_results(void)
{
    char buf[0x28];
    char *pp;
    struct ptgroup grp[10];
    struct podium pods[8];
    short x, y, who, place, x2, y2, who2, place2;
    short np1, npod, r, first, mypod, pod2;
    short dx, dy, phase, dstate, dt, dfr, ft, songdone;
    short i, j, k, n, frame, tick;
    int w;

    if (cursong != v7c658)
        music_start(v7c658, 1);
    np1 = nplayers + 1;
    npod = (np1 != 2) + 2;
    for (k = 1; k <= 8; k++)
        grp[k].n = 0;
    for (i = 0; i <= nplayers; i++) {
        standings[i + 1].score = players[i].f1e;
        standings[i + 1].player = players[i].face;
        if (splitscreen) {
            if (i == 0)
                place = i;
            if (i == 1)
                place2 = i;
        } else if (i == 0)
            place = i;
    }
    swap_n = 2;
    swap_tmp = (struct crec *)standings;
    for (i = 1; i <= nplayers; i++) {
        for (j = i; ++j <= nplayers + 1;) {
            if (standings[i].score < standings[j].score) {
                swap_a = (struct crec *)&standings[i];
                swap_b = (struct crec *)&standings[j];
                rec_swap();
            }
        }
    }
    pcx_load("ORDER.PCX", 0);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    page_draw = 0;
    v88b4a = 0;
    page_draw = 0;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0;
    leaderboard(&x, &y, &who, &place, &x2, &y2, &who2, &place2);
    page_from = 0;
    page_to = 0x7d00;
    page_copy();
    frame = 0;
    sprptr = faces[who].img[0];
    spr_h = 0x26;
    spr_x = x;
    spr_w = 0x1c;
    spr_y = y;
    spr_draw();
    if (splitscreen) {
        sprptr = faces[who2].img[0];
        spr_h = 0x26;
        spr_x = x2;
        spr_w = 0x1c;
        spr_y = y2;
        spr_draw();
    }
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    page_draw = 0x3e80;
    fade_in((unsigned char *)v7d854 = v7d85c);
    page_from = 0x7d00;
    ticks = 0;
    ticks2 = 0;
    w = 0xc;
    for (;;) {
        ticks = 0;
        page_to = page_draw;
        page_copy();
        spr_x = x;
        spr_y = y;
        spr_h = 0x26;
        sprptr = faces[who].img[frame];
        spr_w = 0x1c;
        spr_draw();
        if (splitscreen) {
            spr_x = x2;
            spr_y = y2;
            spr_h = 0x26;
            sprptr = faces[who2].img[frame];
            spr_w = 0x1c;
            spr_draw();
        }
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
        if (netgame) {
            v7ed20 = 0x220;
            v7ed1c = 0x2a8;
            if (netgame != 1) {
                vk88cd5 = 0;
                vk88cdd = 0;
                vk88cd6 = 0;
                vk88cd3 = 0;
                joy88b40 = 0;
                joy88b42 = 0;
                joy88b44 = 0;
                joy88b46 = 0;
            }
            v88cf0 = 0;
            v88cf3 = 0;
            if (netgame == 1) {
                if (joy88b40)
                    v88cf0 = 1;
                else
                    v88cf0 = 0;
            }
            net88c14 = net_key(v88cf0, v88cf3, 0);
            if (netgame != 1 && v88cef == 1)
                joy88b40 = 1;
            if (net88c14) {
                fn_1c068();
                msgbox("EXCESSIVE ERRORS!", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
                goto done;
            }
        }
        page_show();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        if (page_draw == 0)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        while (w >= ticks)
            ;
        if (joy88b40)
            break;
        if (++frame > 7)
            frame = 0;
        ticks = 0;
        ticks2 = 0;
    }
    v88b4a = 0;
    {
        short save;
        p7d714 = p6a978;
        save = page_draw;
        page_draw = 0;
        pcx_blit();
        page_from = 0;
        page_to = 0x3e80;
        page_draw = save;
    }
    page_copy();
    page_draw = 0;
    page_show();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (v88b88 != 5)
        goto done;
    r = 1;
    k = 1;
    do {
        first = r;
        n = 0;
        for (j = r; j <= np1; j++) {
            if (standings[first].score != standings[j].score)
                break;
            grp[k].pl[++grp[k].n] = standings[j].player;
            n++;
        }
        if (n == 0)
            r++;
        else
            r += n;
        k++;
    } while (k <= npod && r <= np1);
    mypod = 0;
    pod2 = 0;
    for (k = 1; k <= 5; k++)
        pods[k].n = 0;
    for (k = 1; k <= npod; k++) {
        pods[k].n = grp[k].n;
        for (j = 1; j <= pods[k].n; j++) {
            n = pods[k].pl[j] = grp[k].pl[j];
            if (n == myplayer)
                mypod = k;
            if (n == v88c6a)
                pod2 = k;
        }
        pods[k].done = 0;
    }
    pods[4].n = 0;
    pods[5].n = 0;
    if (mypod == 0) {
        pods[4].n = 1;
        pods[4].x = 0xa;
        pods[4].y = 0x9e;
        pods[4].pl[1] = myplayer;
    }
    if (splitscreen && pod2 == 0) {
        pods[5].n = 1;
        pods[5].x = 0x110;
        pods[5].y = 0x9e;
        pods[5].pl[1] = v88c6a;
    }
    if (mypod && v7ec80 != 2 && v88bb2 == 3 && v7ed98 && !splitscreen) {
        strcpy(str7c854, " YOU HAVE WON THE RIGHT TO ENTER ");
        strcpy(str7c890, " THE BONUS TRACK! THIS GIVES YOU ");
        strcpy(str7c8cc, " THE CHANCE TO GET MORE POINTS!! ");
        showlines(3, 0, 0x220);
        v7ec80 = 1;
        goto done;
    }
    pods[1].x = 0x8e;
    pods[1].y = 0x96;
    pods[2].x = 0x55;
    pods[2].y = 0x92;
    pods[3].x = 0xc5;
    pods[3].y = 0x8f;
    for (k = 1; k <= npod; k++) {
        pods[k].gx = pods[k].x + 0xb;
        pods[k].gy = 0;
        pods[k].gyend = pods[k].y - (pods[k].n - 1) * 0x1c;
        pods[k].gyend -= 0xb;
    }
    strcpy(buf, "THE ROSTRUM");
    pods[1].g = 2;
    pods[2].g = 1;
    pods[3].g = 0;
    v88c5a = 0;
    x = (0x140 - (int)strlen(buf) * 8) / 2;
    dx = 0;
    dy = 0xaa;
    print_at(x, 0x3c, buf, 0);
    ticks = 0;
    ticks2 = 0;
    pcx_load("WIN.PCX", 0);
    pp = v7d858;
    datfind("WIN.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pp, 0x300);
    pal_6bit_x(pp);
    while (ticks < 0x190)
        ;
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    v88b4a = 0;
    {
        short save;
        p7d714 = p6a978;
        save = page_draw;
        page_draw = 0;
        pcx_blit();
        page_from = 0;
        page_to = 0x7d00;
        page_draw = save;
    }
    page_copy();
    fade_in((unsigned char *)v7d858);
    music88bcc = 0;
    music_start(v7c64c, 0);
    page_draw = 0x3e80;
    phase = 1;
    page_from = 0x7d00;
    ticks = 0;
    ticks2 = 0;
    v88bec = (v88bb6 - 1) * 2;
    frame = 0;
    tick = 0;
    dstate = 0;
    dt = 0;
    ft = 0;
    songdone = 0;
    if (!musicon)
        songdone = 1;
    voice_play(0x12);
    for (;;) {
        v7e724 = ticks;
        page_to = page_draw;
        page_copy();
        if (musicon && !songdone && !MUSIC_SongPlaying()) {
            songdone = 1;
            music_start(menusong, 1);
        }
        switch (phase) {
        case 1:
            spr_h = 0x26;
            spr_w = 0x1c;
            sprptr = sp6a538[frame];
            for (k = 1; k <= 5; k++) {
                spr_y = pods[k].y;
                spr_x = pods[k].x;
                for (j = 1; j <= pods[k].n; j++) {
                    spr_draw();
                    spr_y -= 0x1c;
                }
            }
            if (tick > 2) {
                frame++;
                tick = 0;
            }
            if (frame == 4) {
                ft = 0;
                frame = 5;
                phase = 2;
                tick = 0;
            }
            break;
        case 2:
            if (tick >= 0x20 && dstate == 0) {
                dstate = 1;
                tick = 0;
                dfr = 0;
                dt = tick;
            }
            spr_h = 0x26;
            spr_w = 0x1c;
            for (k = 1; k <= 5; k++) {
                spr_x = pods[k].x;
                spr_y = pods[k].y;
                for (j = 1; j <= pods[k].n; j++) {
                    if (!splitscreen && pods[k].pl[j] == myplayer)
                        sprptr = (char *)*(curkart->f4e = &kd7b378[frame]);
                    else if (k == 4 || k == 5) {
                        if (k == 4)
                            curkart->f4e = &kd7b238[v88c5a][0];
                        else
                            curkart->f4e = &kd7b238[v88c5a][1];
                        sprptr = (char *)*curkart->f4e;
                    } else
                        sprptr = faces[pods[k].pl[j]].img[0];
                    spr_draw();
                    spr_y -= 0x1c;
                }
            }
            if (ft > 2) {
                ft = 0;
                frame++;
            }
            v88c5a++;
            if (frame == v88c52)
                frame = 5;
            if (v88c5a == 8)
                v88c5a = 0;
            if (dstate) {
                sprptr = sp7c800[v88bec + dfr];
                spr_h = 0x1c;
                spr_x = dx;
                spr_w = 0x17;
                spr_y = dy;
                spr_draw();
                if (dt > 1) {
                    dt = 0;
                    dfr++;
                }
                if (dfr == 2)
                    dfr = 0;
                if (dstate == 1) {
                    dx += 0x10;
                    if (dx >= pods[1].x + 6) {
                        dstate = 2;
                        tick = 0;
                        dx = pods[1].x + 6;
                    }
                }
            }
            break;
        }
        if (phase > 1) {
            for (k = 1; k <= npod; k++) {
                sprptr = flags7eb24[pods[k].g];
                spr_h = 0xf;
                spr_x = pods[k].gx;
                spr_w = 0xd;
                spr_y = pods[k].gy;
                spr_draw();
                pods[k].gy += 0xe;
                if (pods[k].gy >= pods[k].gyend) {
                    pods[k].gy = pods[k].gyend;
                    if (!pods[k].done)
                        voice_play(0x17);
                    pods[k].done = 1;
                }
            }
        }
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
        if (netgame) {
            v7ed20 = 0x220;
            v7ed1c = 0x2a8;
            if (netgame != 1) {
                vk88cd5 = 0;
                vk88cdd = 0;
                vk88cd6 = 0;
                vk88cd3 = 0;
                joy88b40 = 0;
                joy88b42 = 0;
                joy88b44 = 0;
                joy88b46 = 0;
            }
            v88cf0 = 0;
            v88cf3 = 0;
            if (netgame == 1) {
                if (joy88b40)
                    v88cf0 = 1;
                else
                    v88cf0 = 0;
            }
            net88c14 = net_key(v88cf0, v88cf3, 0);
            if (netgame != 1 && v88cef == 1)
                joy88b40 = 1;
            if (net88c14) {
                fn_1c068();
                msgbox("EXCESSIVE ERRORS!", 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
                for (i = 0; i <= 0x2ff; i++)
                    p7d718[i] = v7d858[i];
                goto done;
            }
        }
        page_show();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        while (v7e724 + 6 > ticks)
            ;
        dt++;
        ft++;
        tick++;
        if (page_draw == 0)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        if (joy88b40) {
            if (mypod != 1)
                break;
        }
        if (joy88b40 && splitscreen)
            break;
        if (mypod == 1 && joy88b40) {
            scores_screen();
            break;
        }
    }
    for (i = 0; i <= 0x2ff; i++)
        p7d718[i] = v7d858[i];
done:
    page_draw = 0x3e80;
}

/* ---- 0x2d264, 406 bytes: the result message (caller 0x31896 in 0x30aae) into str6aa18 with
 * v88c48 set and centred at v88b66 (8 px per character): in time trial (v88bb2 4) TIMES HAVE
 * BEEN RECORDED, else by the place of player v88b7e the line of race v88b88 from the 0x28-byte
 * tables 0x63d44 (1st), 0x63e0c (2nd), 0x63ed4 (3rd). The case bodies keep a dead v88bb2 == 4
 * branch (YOU ARE (TOO) GOOD! TRY A REAL RACE!, except in race 5) that the game also has. */
extern char praise1[][0x28], praise2[][0x28], praise3[][0x28];
#pragma aux praise1 "_arr63d44"
#pragma aux praise2 "_arr63e0c"
#pragma aux praise3 "_arr63ed4"

void result_msg(void)
{
    v88c48 = 1;
    if (v88bb2 == 4)
        strcpy(str6aa18, "TIMES HAVE BEEN RECORDED");
    else {
        switch (players[v88b7e].place) {
        case 1:
            if (v88bb2 == 4) {
                if (v88b88 != 5)
                    strcpy(str6aa18, "YOU ARE TOO GOOD! TRY A REAL RACE!");
            } else
                strcpy(str6aa18, praise1[v88b88 - 1]);
            break;
        case 2:
            if (v88bb2 == 4) {
                if (v88b88 != 5)
                    strcpy(str6aa18, "YOU ARE GOOD! TRY A REAL RACE!");
            } else
                strcpy(str6aa18, praise2[v88b88 - 1]);
            break;
        case 3:
            if (v88bb2 == 4) {
                if (v88b88 != 5)
                    strcpy(str6aa18, "YOU ARE GOOD! TRY A REAL RACE!");
            } else
                strcpy(str6aa18, praise3[v88b88 - 1]);
            break;
        }
    }
    v88b66 = (0x140 - (int)strlen(str6aa18) * 8) / 2;
}

/* ---- 0x2d3fc, 255 bytes: a pending turn bump (v88bf4; callers 0x2f7a5, 0x2fa2c in 0x2f794):
 * curpos turns by 0x12 (left with v88bfe 5, else right) in 0..0x780, the move vector is
 * applied, and on v88bfc 2 / 1 with v88bf6 curkart->f42 rises / falls by 0x28 within 0..0xbe. */
void bump_turn(void)
{
    if (v88bf4 == 0)
        return;
    if (v88bfe == 5) {
        curpos -= 0x12;
        if (curpos < 0)
            curpos += 0x780;
    } else {
        curpos += 0x12;
        if (curpos >= 0x780)
            curpos -= 0x780;
    }
    v7d678 = curpos;
    track_around();
    VEC(v7d614, v7d5e4, 1);
    view_x += vec_kx;
    view_y += vec_ky;
    if (v88bfc == 2 && v88bf6) {
        curkart->f42 += 0x28;
        if (curkart->f42 > 0xbe)
            curkart->f42 = 0xbe;
    } else if (v88bfc == 1 && v88bf6) {
        curkart->f42 -= 0x28;
        if (curkart->f42 < 0)
            curkart->f42 = 0;
    }
    v88bf4 = 0;
}

/* ---- 0x2d4fc, 73 bytes: clears cars[0..1].f02/f04 and every shot's live flag f04 (caller
 * 0x300fb); v7d874 is left past the table.
 * MATCH (iter 94): the shot pointer is the global v7d874 walked in place - there is no local for it,
 * and the game's EBX is the compiler's cached global (`mov ebx,0x7f2b0` 0x2d4fe, one store
 * `mov [v7d874],ebx` 0x2d53c); a local `p` gives EBX to the counter instead. */
void shots_clear(void)
{
    short i;

    v7d874 = shots;
    for (i = 0; i <= 1; i++) {
        cars[i].f02 = 0;
        cars[i].f04 = 0;
    }
    for (i = 0; i <= 0x3c; i++) {
        (v7d874++)->f04 = 0;
    }
}

/* ---- 0x2d548, 223 bytes: launches a shot of kind for player (callers in 0x24364): the
 * first free slot (f04 0) gets speed 0x10 for kinds 1 and 6 (else 0xa), range 0x3e8, the
 * direction v7eb9c and position (v88b68, v88b6a); shot_step(0) moves it off the kart, then
 * the animation (v7eb04, v88bc0 frames of 0xea). v7d874 ends on the shot (or past the table).
 * MATCH (iter 37): the loop walks the global v7d874 itself (the game caches it in EDX, stores it
 * before shot_step, reloads it after - GOTCHAS cached-global lever), and v7d5d0 = curpos sits
 * after the y store (found by tools/wc/permute.py, round 1). */
void shot_fire(short kind, short player)
{
    short i;

    v7d874 = shots;
    for (i = 0; i <= 0x3c; i++, v7d874++) {
        if (v7d874->f04 == 0) {
            if (kind == 1)
                v7d874->speed = 0x10;
            else
                v7d874->speed = 0xa;
            if (kind == 6)
                v7d874->speed = 0x10;
            v7d874->f08 = 1;
            v7d874->f06 = 0;
            v7d874->f04 = 1;
            v7d874->range = 0x3e8;
            v7d874->kind = kind;
            v7d874->owner = player;
            v7d874->dir = v7eb9c;
            v7d874->x = v88b68;
            v7d874->y = v88b6a;
            v7d5d0 = curpos;
            shot_step(0);
            v7d874->dist = 0;
            v7d874->frame = 0;
            v7d874->fsize = 0xea;
            v7d874->spr = v7eb04;
            v7d874->nframes = v88bc0;
            return;
        }
    }
}

/* ---- 0x2d628 / 0x2d67c, 81 bytes each: the GIG sound of a car's banner (gigs[n - 1] at sfxvol, n-- kept apart,
 * priority 0x22): gig_sound2 for the second banner fb0 (handle fc8, playing flag fcc; overlay_draw),
 * car_finished for the lap banner fdc (handle fec, flag ff0; car_laps, lap_banner). */
void gig_sound2(int i)
{
    int n;

    n = cars[i].fb0;
    n--;
    cars[i].fc8 = FX_PlayVOC(gigs[n], 0, sfxvol, sfxvol, sfxvol, 0x22, 0);
    if (cars[i].fc8 > 0)
        cars[i].fcc = 1;
}

void car_finished(int i)
{
    int n;

    n = cars[i].fdc;
    n--;
    cars[i].fec = FX_PlayVOC(gigs[n], 0, sfxvol, sfxvol, sfxvol, 0x22, 0);
    if (cars[i].fec > 0)
        cars[i].ff0 = 1;
}

/* ---- 0x2d6d0, 634 bytes: the human kart's special buttons (callers in 0x30aae). Outside race
 * state 2 only fire stays; in two-player / split screen a pending v88ce0 starts the second
 * banner (fbc counts them, voice 0x1a from the sixth) with its GIG sound. Keys and buttons are
 * dropped while the kart is busy (f20, f7e, f06, v88c3c) or players[].f58 runs. Outside v7d998
 * modes, buttons 2 + 1 with ability bit 1 start a boost (fb4, f42 0xa0, speed tbl7e730), and
 * 2 + 0 with bit 2 a jump (f0a; ends a f44 state). */
extern unsigned char v88ce0;

void kart_buttons(void)
{
    if (v88c44 != 2) {
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
    }
    if (twoplayer || splitscreen) {
        if (v88ce0 && curkart->fbc >= 6) {
            v88ce0 = 0;
            voice_play(0x1a);
        }
        if (v88ce0 && curkart->fc0 == 0) {
            curkart->fc4 = 0;
            curkart->fc8 = 0;
            curkart->fcc = 0;
            curkart->fbc++;
            curkart->fc0 = v88ce0;
            if (soundon) {
                if (curkart->fdc) {
                    FX_StopSound(curkart->fd8);
                    curkart->fdc = 0;
                }
                gig_sound2(v88b7e);
            }
        }
    }
    if (curkart->f20 || v88c3c || curkart->f7e || curkart->f06 || players[v88b7e].f58) {
        key88cd3 = 0;
        key88cd5 = 0;
        key88cdd = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
    }
    if (v7d998)
        return;
    if (joy88b44 && joy88b42 && (curkart->fe0 & 1)) {
        if (curkart->fb4 == 0) {
            curkart->fb4 = 1;
            curkart->f42 = 0xa0;
            curkart->fb6 = 0x10;
            curkart->f26 = tbl7e730[curkart->f42];
            voice_play(0xc);
        }
        joy88b44 = 0;
        joy88b46 = 0;
        return;
    }
    if (joy88b44 && joy88b40 && (curkart->fe0 & 2)) {
        if (curkart->f0a == 0) {
            if (curkart->f44) {
                curkart->f44 = 0;
                players[v88b7e].f5c = 0;
                curkart->f5a = 0;
            }
            curkart->f0a = 1;
            voice_play(0xc);
            curkart->f0c = 8;
            curkart->f10 = 8;
            v88bae = 4;
            curkart->f0e = 0x28;
        }
        joy88b44 = 0;
        joy88b40 = 0;
    }
}

/* ---- 0x2d94c, 92 bytes: the received key byte v88cef as the second set of controls (caller
 * 0x31fb9): bits 1/2 keys cd5/cdd, 4..0x20 buttons b40..b46, 0x40 v88cec. Reads v88cef in every
 * statement, no local (iter 93s: the local-c spellings put c in AL; the game's DL is the compiler's
 * own temp for the global, findings/compiler.md). */
void netkeys_in(void)
{
    key88cd5 = v88cef & 1;
    key88cdd = v88cef & 2;
    joy88b40 = v88cef & 4;
    joy88b42 = v88cef & 8;
    joy88b44 = v88cef & 0x10;
    joy88b46 = v88cef & 0x20;
    v88cec = v88cef & 0x40;
}

/* ---- 0x2d9a8, 57 bytes (no caller found): the same byte as keys cd6/cd3, button b40, v7ecc0. */
void netkeys_in1(void)
{
    key88cd6 = v88cef & 1;
    key88cd3 = v88cef & 2;
    joy88b40 = v88cef & 4;
    v7ecc0 = v88cef & 0x40;
}

/* ---- 0x2d9e4, 65 bytes (no caller found): packs keys cd6/cd3, button b40 and v7ecc0 into the
 * byte v88cf0 to send (v88cf3 cleared).
 * Builds the byte in v88cf0 itself, no local (iter 93s: every local-c spelling put c in AL; the
 * game's DL is the compiler's temp for the global, findings/compiler.md). */
void netkeys_out(void)
{
    v88cf0 = 0;
    v88cf3 = v88cf0;
    if (key88cd6)
        v88cf0 = 1;
    if (key88cd3)
        v88cf0 += 2;
    if (joy88b40)
        v88cf0 += 4;
    if (v7ecc0)
        v88cf0 += 0x40;
}

/* ---- 0x2da28, 104 bytes: packs all local controls (keys cd5/cdd, buttons b40..b46, v88cec)
 * into the byte v88cf0 to send (callers 0x19811, 0x1ac3f); v88cf3 cleared.
 * Builds the byte in v88cf0 itself, no local (iter 93s: every local-c spelling put c in AL; the
 * game's DL is the compiler's temp for the global, findings/compiler.md). */
void netkeys_pack(void)
{
    v88cf0 = 0;
    v88cf3 = v88cf0;
    if (key88cd5)
        v88cf0 = 1;
    if (key88cdd)
        v88cf0 += 2;
    if (joy88b40)
        v88cf0 += 4;
    if (joy88b42)
        v88cf0 += 8;
    if (joy88b44)
        v88cf0 += 0x10;
    if (joy88b46)
        v88cf0 += 0x20;
    if (v88cec)
        v88cf0 += 0x40;
}

/* ---- 0x2da90, 111 bytes: com_getc (inlined) retried until a byte comes or timeout ticks
 * pass; -1 on timeout. int, not short: net_recv uses the inlined result without a cwde. */
int com_getc_wait(int timeout)
{
    int t0;
    int c;

    t0 = ticks;
    do {
        c = com_getc();
        if (c != -1)
            return c;
    } while (ticks - t0 < timeout);
    return -1;
}

/* ---- 0x2db00, 312 bytes: receives one packet (caller 0x2de44 in 0x2dd30): six values, the
 * first 'Z', 'D' or 'W' (other leading bytes count as noise; after v7ed1c ticks of noise
 * v7ed60 = v7ed34 and give up), each within v7ed20 ticks. Fewer than six: timeouts, 'N'. The
 * CRC of the first four (crcupdate) must equal the bytes 4 and 5, else badcrc, 'N'. A 'D'
 * packet carries v88cf6 / v88cf5; v88cb0 = value 3; returns the type. v7ed60 counts errors.
 * differs: only the CRC accumulator's register (ECX in the game, EAX/EDX here); its
 * declaration position and reusing n were tried. Iter 17 (21 spellings, two probe compiles): the
 * zero-assignment order (`crc = 0;` first, `crc = rcrc = 0;`, `rcrc = crc = 0;`), a `unsigned short`
 * / `int` temp around each crcupdate call, `k != 4` / `k = k + 1` / `while`, casts on the argument
 * (`(unsigned short)`, `(short)`, `& 0xffff`), a merged `rcrc, crc` declaration, `crc` declared last
 * and `p` first, an `if (k == 3) break;` guard and swapping `n = 0; t = ticks;` all give the same
 * 4 rows. `int crc` does move the home to ECX (the loop-carried value then needs a widening move):
 * `movzx ecx,ax` + `movzx eax,cx` where the game has `movzx eax,ax` (the value stays in EAX for the
 * 2nd arg) + `mov ecx,eax`, so the whole file goes 4 rows -> 5. No spelling tried moves crc's home
 * register without some other change. */
extern int v7ed60;
extern unsigned char v88cf6, v88cf5;
extern short v88cb0;

int net_recv(void)
{
    int buf[10];
    unsigned short rcrc;
    char *p;
    unsigned short crc;
    int n, t, c, k;

    p = (char *)&rcrc;
    n = 0;
    t = ticks;
    for (;;) {
        c = com_getc_wait(v7ed20);
        if (c == -1)
            break;
        if (n == 0 && c != 'Z' && c != 'D' && c != 'W') {
            noise++;
            if (ticks - t > v7ed1c) {
                v7ed60 = v7ed34;
                break;
            }
            continue;
        }
        t = ticks;
        buf[n] = c;
        if (n == 5)
            break;
        n++;
    }
    if (n < 5) {
        timeouts++;
        v7ed60++;
        return 'N';
    }
    rcrc = 0;
    crc = 0;
    for (k = 0; k < 4; k++)
        crc = crcupdate(crc, buf[k]);
    *p++ = buf[4];
    *p = buf[5];
    if (crc != rcrc) {
        badcrc++;
        v7ed60++;
        return 'N';
    }
    if (buf[0] == 'D') {
        v88cf6 = buf[1];
        v88cf5 = buf[2];
    }
    v88cb0 = buf[3];
    return buf[0];
}

/* ---- 0x2dc38, 247 bytes: sends one packet (callers in 0x2dd30): type, a, b (zero for 'Z' and
 * 'W'), v7ed8c, then the CRC of those four, low byte first. buf[12] puts the CRC at esp+0xc
 * as in the game; the send loop is unrolled by the compiler, which keeps buf in memory. */
extern int v7ed8c;

void net_send(int type, unsigned char a, unsigned char b)
{
    unsigned char buf[12];
    unsigned char c3;
    unsigned short crc;
    unsigned char *p;
    int i;

    if (type == 'Z' || type == 'W') {
        a = 0;
        b = 0;
    }
    c3 = v7ed8c;
    crc = 0;
    crc = crcupdate(crc, type);
    crc = crcupdate(crc, a);
    crc = crcupdate(crc, b);
    crc = crcupdate(crc, c3);
    buf[1] = a;
    buf[2] = b;
    buf[3] = c3;
    p = (unsigned char *)&crc;
    buf[4] = *p;
    p++;
    buf[0] = type;
    buf[5] = *p;
    for (i = 0; i < 6; i++)
        com_putc(buf[i]);
}

/* ---- 0x2dd30, 576 bytes: one exchange of the control bytes with the other machine (key, flag;
 * x set while racing). A 'D' packet carries ours, the reply 'D' carries theirs (into v88cef /
 * v88cf2) and is acknowledged with 'Z'; 'W' means wait. The sequence byte v7ed8c advances when
 * the exchange completes; a stale packet (v88cb0 < v88cae) makes us resend 'W'. With x the game
 * draws (view_box, sprites_build, 0x2f794) while waiting. A half-done exchange is parked in
 * v7ed94 / v7edac / v7edb8 / v88cf1 / v88cf4 and resumed by the next call. Returns 1 once more
 * than v7ed34 errors (v7ed60) pile up, else 0. The inner switch has case 'Z' first (layout).
 * v88cae is the low byte of v7ed8c: `mov ax,[v7ed8c]; xor ah,ah; mov [v88cae],ax` at
 * 0x2dd42-0x2dd4e, so the load goes through a word-typed alias of v7ed8c (w7ed8c). */
extern int v7ed6c, v7ed74, v7edac, v7edb8, v7ed94;
extern short v88cae;
extern short w7ed8c;
#pragma aux w7ed8c "_v7ed8c"
extern unsigned char v88cf1, v88cf4;
extern unsigned char b88cf2;
#pragma aux b88cf2 "_v88cf2"
void race_frame2(void);
void view_box_x(void);
#pragma aux view_box_x "view_box_"
void sprites_build_x(void);
#pragma aux sprites_build_x "sprites_build_"

short net_key(unsigned char key, unsigned char flag, int x)
{
    int state, send, wait, resend, r, seq, t;

    v88cae = w7ed8c & 0xff;
    wait = 0;
    resend = 0;
    if (v7ed94) {
        v88cef = v88cf1;
        b88cf2 = v88cf4;
        v88cf6 = v88cef;
        v88cf5 = b88cf2;
        net_send('D', key, flag);
        send = 'Z';
        state = send;
        wait = v7edac;
        resend = v7edb8;
    } else {
        state = 'D';
        send = state;
    }
    v7ed94 = 0;
    v7ed60 = 0;
    net88c14 = 0;
    for (;;) {
        if (!wait) {
            if (!resend)
                net_send(send, key, flag);
            else
                net_send('W', 0, 0);
        }
        if (x) {
            if (!v7ed6c) {
                v7ed6c = 1;
                view_box_x();
                if (v7eb80)
                    floor_draw();
                sprites_build_x();
            }
            if (v7ed6c && !v7ed74)
                race_frame2();
        }
        r = net_recv();
        if (v88cb0 < v88cae && r != 'N') {
            resend = 1;
            continue;
        }
        resend = 0;
        if (wait && (r == 'D' || r == 'Z'))
            wait = 0;
        seq = v7ed8c + 1;
        switch (r) {
        case 'W':
            wait = 1;
            break;
        case 'D':
            t = ticks;
            switch (state) {
            case 'Z':
                if (v88cb0 > v88cae) {
                    v7ed94 = 1;
                    v7edac = wait;
                    v7edb8 = resend;
                    v88cf1 = v88cf6;
                    v88cf4 = v88cf5;
                    v7ed8c = seq;
                    return 0;
                }
                send = 'D';
                break;
            case 'D':
                v88cef = v88cf6;
                send = 'Z';
                state = send;
                b88cf2 = v88cf5;
                break;
            }
            break;
        case 'Z':
            switch (state) {
            case 'D':
                send = 'D';
                break;
            case 'Z':
                v7ed8c = seq;
                return 0;
            }
            break;
        default:
            if (v7ed60 > v7ed34)
                return 1;
        }
    }
}

/* ---- 0x2df70, 152 bytes (no caller found): com_getc until a byte comes; without esc gives up
 * after 0x110 ticks (net88c14 = 1), with esc when key 0xcd0 is 1 (net88c14 = 2); 0 then. The
 * start time is left in v7ea68.
 * differs: registers only (game esc in EDX, t0 in EBX; ours the reverse); local order, the d
 * temp, do/goto form and short / char esc tried. All 9 rows are those three homes: esc EBX/EDX,
 * t0 EDX/EBX and the `d` temp EAX/ECX (the tail `lea eax,[eax]` vs `nop` row is the 1-byte size
 * gap of the EAX-only `mov eax,[abs]` (A1, 5 bytes) vs `mov ecx,[abs]` (8B 0D, 6), not a fourth
 * thing). 16 probe spellings (types, orders, register, an esc copy, while, no d, swapped arms)
 * and `unsigned d` whole-file change nothing; a standalone probe of this function scores 24 for
 * the control where src/ww.c scores 9, so probe ranks do not transfer here. */
extern int v7ea68;

int com_wait_byte(int esc)
{
    int t0, c, d;

    t0 = ticks;
    for (;;) {
        c = com_getc();
        if (c != -1)
            break;
        if (!esc) {
            d = ticks - t0;
            if (d >= 0x110) {
                net88c14 = 1;
                c = 0;
                break;
            }
        } else if (key88cd0 == 1) {
            net88c14 = 2;
            c = 0;
            break;
        }
    }
    v7ea68 = t0;
    return c;
}

/* ---- 0x2e008, 158 bytes: com_getc until the byte want arrives (1); key 0xcd0 aborts
 * (net88c14 = 2, 0); a non-zero timeout gives up after that many ticks (0). v7ea68 = start.
 * differs: registers only: want in EBX and t0 in ESI in the game, the reverse here (as in
 * com_wait_byte); all 30 orders of t0, c, r (and an elapsed temp) tried. Iter 22 closed the rest:
 * net_sync 0x2e0fc is byte-identical, so want really does arrive in EAX and timeout in EDX (the
 * parameter order is not the cause); 10 more spellings in one probe compile
 * (.work/probe_comexpect/CE.C, control 9 = the fndiff count, so the ranking applies) - an elapsed
 * local d in three declaration positions, an unsigned d, the elapsed written into r, a copy of
 * want in a local, t0 = ticks first, short want - leave the two homes alone; the row at 0x2e03b
 * (`[eax - @]` vs `[eax + @]`) is a fndiff masking artifact, not a row to fix (GOTCHAS); and the
 * pair `mov ebx,eax` + `mov esi,[abs]` occurs exactly once in the whole image (0x2e016), so no
 * matched function anywhere shows the game's spelling. GOTCHAS "Which pseudo gets EAX is a coin
 * flip": this is that case. */
int com_expect(int want, int timeout)
{
    int t0, c, r;

    net88c14 = 0;
    t0 = ticks;
    for (;;) {
        c = com_getc();
        if (key88cd0 == 1) {
            net88c14 = 2;
            r = 0;
            break;
        }
        if (c != -1 && c == want) {
            r = 1;
            break;
        }
        if (timeout && ticks - t0 >= timeout) {
            r = 0;
            break;
        }
    }
    v7ea68 = t0;
    return r;
}

/* ---- 0x2e0a8, 82 bytes: drops everything in the receive buffer. */
void com_flush(void)
{
    while (com_getc() != -1)
        ;
}

/* ---- 0x2e0fc, 315 bytes: starts a link (caller 0x20a86 in main_menu): clears the packet
 * state and counters, sets the hang-up text NO CARRIER, drains the line, then player one
 * (netgame 1) sends 0x7f until a 0x7f comes back (0x28-tick waits), the other side waits for
 * 0x7f and answers it. Returns 1 when aborted (net88c14), else 0 with net7ed78 set. */
extern short v88cac, v88cb2;
extern char str7d2d8[];
#pragma aux str7d2d8 "_arr7d2d8"

short net_sync(void)
{
    int r;

    v88cac = 0;
    v88cb2 = 0;
    v7ecc4 = 0;
    v7ed94 = 0;
    v7ed8c = 0;
    v88cb0 = 0;
    badcrc = 0;
    timeouts = 0;
    noise = 0;
    net88c14 = 0;
    v7ed60 = 0;
    strcpy(str7d2d8, "NO CARRIER");
    com_flush();
    if (netgame == 1) {
        do {
            r = com_expect(0x7f, 0x28);
            if (net88c14)
                return 1;
            if (!r) {
                com_putc(0x7f);
                r = com_expect(0x7f, 0x28);
            }
            if (net88c14)
                return 1;
        } while (!r);
        com_flush();
    } else {
        com_expect(0x7f, 0);
        if (net88c14)
            return 1;
        com_putc(0x7f);
        com_flush();
    }
    net7ed78 = 1;
    return 0;
}

/* ---- 0x2e238..0x2e3f4: demo recording and playback (v7d998 1 = record, 2/3 = play). The
 * recording is the .MAC file (one control byte per frame, demo_rec); playback reads the .BMC
 * entry, runs of (count, control byte) at 0x7d9a0. */
#pragma pack(1)
struct demorun { short n; unsigned char k; };
#pragma pack()
extern struct demorun demo7d9a0[];
#pragma aux demo7d9a0 "_arr7d9a0"
extern int v7d994, v7e564;
extern FILE *v7eb68;
extern unsigned char v88cca;
extern volatile unsigned char v88ccav;    /* the same byte, declared volatile: demo_play's store
                                             must not be deferred (0x2e361); the alias keeps _v88cca */
#pragma aux v88ccav "_v88cca"

/* 0x2e238, 145 bytes (caller 0x30f77): record: opens <level>.MAC for writing; play: first run. */
void demo_start(void)
{
    if (v7d998 == 1) {
        strcpy((char *)lit7d7b8, (char *)lit7d770);
        strcat((char *)lit7d7b8, ".MAC");
        v7eb68 = fopen((char *)lit7d7b8, "wb");
        return;
    }
    v7d994 = 0;
    v7e564 = demo7d9a0[0].n;
}

/* 0x2e2cc, 75 bytes (no caller found): loads the runs of DAT entry name. */
void demo_load(char *name)
{
    short n;
    int m;

    datfind(name);
    fread(&n, 2, 1, datfile);
    m = n * 3;
    fread(demo7d9a0, m, 1, datfile);
}

/* 0x2e318, 47 bytes (no caller found): packs this frame's controls and appends them.
 * differs: only the call displacement to netkeys_pack (the near misses before it are 4 bytes
 * shorter here in total); the code is identical. */
void demo_rec(void)
{
    netkeys_pack();
    v88cca = v88cf0;
    fwrite(&v88cca, 1, 1, v7eb68);
}

/* 0x2e348, 169 bytes (no caller found): sets this frame's controls from the current run.
 * differs (4 rows): the game's byte is loaded into BL, k's home, and its store to v88cca sits at
 * 0x2e361, between the copy `mov al,bl` (0x2e35f) and `and al,1` (0x2e367). Writing the load and the
 * store as two statements makes 9.5b load into BL and store from BL (172 bytes, the game's length);
 * v88ccav (v88cca declared volatile, aliased to _v88cca) keeps that store there, and the one row
 * left is the copy one slot late: 9.5b emits store,copy,and, the game copy,store,and. Tried: the
 * chained forms both ways, char/short/int/unsigned k, register, a second byte local carrying the
 * first mask (7 spellings: it copies too early or late), mask1 through a temp, mask1 with the store
 * inside the AND's left or right operand (comma forms), a local pointer to v88cca, the store at
 * every one of the nine source positions, and a read of v88cca after the loop (46 spellings, six
 * probe compiles, .work/probe_dplay). */
void demo_play(void)
{
    unsigned char k;

    k = demo7d9a0[v7d994].k;
    v88ccav = k;
    key88cd5 = k & 1;
    key88cdd = k & 2;
    joy88b40 = k & 4;
    joy88b42 = k & 8;
    joy88b44 = k & 0x10;
    joy88b46 = k & 0x20;
    v88cec = k & 0x40;
    if (--v7e564 <= 0) {
        v7d994++;
        v7e564 = demo7d9a0[v7d994].n;
    }
    v88cef = k;
}

/* ---- 0x2e3f4, 172 bytes (no caller found): pads menu rows 1..n with spaces to the length of
 * row a. */
void rows_pad(int a, int n)
{
    int len, i;

    len = strlen(mrows[a]);
    for (i = 1; i <= n; i++)
        while (strlen(mrows[i]) < len)
            strcat(mrows[i], " ");
}

/* ---- 0x2e4b8, 4080 bytes: the TIME TRIAL hall of fame for track b of group a (1..3 bronze /
 * silver / gold, 4..6 bonus). With show set (after a time trial, caller 0x31e98) the six lap
 * times of the run (laptimes) and the race time v7eccc are shown first; a best lap that makes
 * the top four, or a race time at least as good as the record, asks for a name (enter_name)
 * and goes into the book, trial70730[(a - 1) * 5 + b - 1]. Records are marked with a blinking
 * pointer (gn7eae4) until fire / Esc / a joystick button. The menu calls (0x1911f, 0x1d6ca)
 * only show the book.
 * differs: 121 rows, 92 of them the scalar slot permutation (dead end; frame 0x15c and the array
 * order title, name, buf, num already match the game's). The real rows: the record-row loop's two
 * homes are the mirror image of the game's (game counts in EBP and keeps the row pointer in slot
 * [esp+0x110], stepped there by 0x14 and reloaded into ESI each pass, 0x2ec8e/0x2edb4; ours keeps
 * the pointer in EBP and counts in a slot), ox/oy swap EBX/EDX (0x2f0f8), and at 0x2ed73 the game
 * tests its register counter where we reload the slot. Tried (iter 41, probe .work/probe_tb/TB.C,
 * control 155): the sounds_load pointer lever in four spellings - `char *nm` assigned before the
 * loop and stepped in the header, at the body end, in the for init, declared after the ints -
 * 256/256/279/302: the extra local re-numbers every slot (the probe control scores 155 there, so
 * probe scores rank this function badly - decide with fndiff on the whole file). */
struct trialrec {
    char lapname[4][20];
    int lap[4];
    char racename[20];
    int race;
};
extern struct trialrec trial70730[];
#pragma aux trial70730 "_arr70730"
void trial_write(int n);
void print_at_s(short x, short y, char *s, short mode);
#pragma aux print_at_s "print_at_"

void trial_book(int a, int b, int show)
{
    char title[0x50];
    char name[20];
    char buf[0x50];
    char num[0x50];
    int best, k, blink, idx, ly, y, pos, first, oy, entered, done, j, ox, i;
    int newrace, ny, my, newlap, my2, bestlap, ry, nx, mx, rx;

    pcx_load("ORDER.PCX", 0);
    palptr = pal7d850;
    getpalette();
    entered = 0;
    fade_out(pal7d850);
    v88b4a = 0;
    page_draw = 0;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0;
    switch (a) {
    case 1:
        strcpy(title, "BRONZE WHEELS. TRACK ");
        idx = b;
        break;
    case 2:
        strcpy(title, "SILVER WHEELS. TRACK ");
        idx = b + 5;
        break;
    case 3:
        strcpy(title, "GOLD WHEELS. TRACK ");
        idx = b + 10;
        break;
    case 4:
        strcpy(title, "BONUS BRONZE . TRACK ");
        idx = b + 15;
        break;
    case 5:
        strcpy(title, "BONUS SILVER. TRACK ");
        idx = b + 20;
        break;
    case 6:
        strcpy(title, "BONUS GOLD. TRACK ");
        idx = b + 25;
        break;
    }
    idx--;
    itoa(b, num, 10);
    strcat(title, num);
    if (v7ecb8 == 1)
        strcat(title, ". 12HP");
    else
        strcat(title, ". 6HP");
    pos = -1;
    newrace = pos;
    newlap = pos;
    bestlap = pos;
    if (show) {
        best = laptimes[0];
        for (i = 0; i <= 5; i++) {
            if (best > laptimes[i]) {
                bestlap = i;
                best = laptimes[i];
            }
        }
        for (j = 0; j <= 3; j++) {
            if (best <= trial70730[idx].lap[j]) {
                pos = j;
                break;
            }
        }
        if (pos == -1) {
            bestlap = pos;
            if (v7eccc <= trial70730[idx].race)
                pos = 6;
        }
        if (pos != -1) {
            music_start(v7c64c, 0);
            enter_name(name, "TIME TRIAL - HALL OF FAME!", 15);
            palptr = pal7d850;
            getpalette();
            fade_out(pal7d850);
            v88b4a = 0;
            page_draw = 0;
            p7d714 = p6a978;
            pcx_blit();
            entered = 1;
            page_draw = 0;
            if (v7eccc <= trial70730[idx].race) {
                trial70730[idx].race = v7eccc;
                strcpy(trial70730[idx].racename, name);
                newrace = 1;
            }
            if (pos != 6) {
                for (j = 0; j <= 3; j = j + 1) {
                    if (best <= trial70730[idx].lap[j]) {
                        if (j != 3)
                        for (k = 3; k >= j + 1; k--) {
                            trial70730[idx].lap[k] = trial70730[idx].lap[k - 1];
                            strcpy(trial70730[idx].lapname[k], trial70730[idx].lapname[k - 1]);
                        }
                        newlap = j;
                        trial70730[idx].lap[j] = best;
                        strcpy(trial70730[idx].lapname[j], name);
                        break;
                    }
                }
            }
        }
    }
    page_draw = 0;
    y = 8;
    spr_x = center_x(strlen(title) * 11);
    wtext(spr_x, y, title, 0);
    y += 20;
    if (show) {
        my2 = 0x12;
        for (i = 0; i <= 5; i++) {
            strcpy(buf, "LAP ");
            itoa(i + 1, num, 10);
            strcat(buf, num);
            strcat(buf, " - ");
            time_str(laptimes[i], num);
            strcat(buf, num);
            print_at_s(center_x(strlen(buf) * 8), y, buf, 0);
            if (bestlap == i) {
                mx = 2 + spr_x;
                my = my2;
            }
            my2 += 10;
            y += 10;
        }
        strcpy(buf, "RACE - ");
        time_str(v7eccc, num);
        strcat(buf, num);
        y += 6;
        print_at_s(center_x(strlen(buf) * 8), y, buf, 0);
        y += 16;
    }
    strcpy(buf, "TIME TRIAL HALL OF FAME");
    spr_x = center_x(strlen(buf) * 11);
    wtext(spr_x, y, buf, 0);
    y += 20;
    if (!show) {
        strcpy(buf, "TOP FOUR LAP TIMES");
        spr_x = center_x(strlen(buf) * 11);
        wtext(spr_x, y, buf, 0);
        y += 20;
    }
    ly = y - 10;
    for (j = 0; j <= 3; j = j + 1) {
        strcpy(buf, trial70730[idx].lapname[j]);
        strcat(buf, " ");
        time_str(trial70730[idx].lap[j], num);
        strcat(buf, num);
        spr_x = center_x(strlen(buf) * 8);
        print_at_s(spr_x, y, buf, 0);
        if (j == newlap) {
            nx = spr_x + 2;
            ny = ly;
        }
        ly += 10;
        y += 10;
    }
    y += 10;
    if (!show) {
        strcpy(buf, "BEST TIME FOR ENTIRE RACE");
        spr_x = center_x(strlen(buf) * 11);
        wtext(spr_x, y, buf, 0);
        y += 20;
        strcpy(buf, "");
    } else {
        strcpy(buf, "RACE - ");
    }
    strcat(buf, trial70730[idx].racename);
    strcat(buf, " ");
    time_str(trial70730[idx].race, num);
    strcat(buf, num);
    spr_x = center_x(strlen(buf) * 8);
    print_at_s(spr_x, y, buf, 0);
    rx = spr_x + 2;
    ry = y - 10;
    if (!show) {
        strcpy(buf, "SEE IF YOU CAN BEAT THESE!");
        y = 0x9e;
        spr_x = center_x(strlen(buf) * 8);
        print_at_s(spr_x, y, buf, 0);
        strcpy(buf, "PRESS FIRE");
        y = 0xb4;
        spr_x = center_x(strlen(buf) * 8);
        print_at_s(spr_x, y, buf, 0);
    }
    page_draw = 0;
    if (entered)
        trial_write(v7ee5a);
    if (cursong != v7c658)
        music_start(v7c658, 1);
    page_from = 0;
    page_to = 0x7d00;
    first = 1;
    page_copy();
    blink = 0;
    page_from = 0x7d00;
    wait_release(0);
    do {
        ticks = 0;
        page_to = page_draw;
        page_copy();
        if (blink) {
            ox = oy = 2;
            blink = 0;
        } else {
            blink = 1;
            ox = oy = 0;
        }
        spr_h = 12;
        spr_w = 13;
        sprptr = gn7eae4;
        if (newlap != -1) {
            spr_x = nx - ox;
            spr_y = oy + ny;
            spr_draw();
        }
        if (bestlap != -1) {
            spr_x = mx - ox;
            spr_y = my + oy;
            spr_draw();
        }
        if (newrace != -1) {
            spr_x = rx - ox;
            spr_y = ry + oy;
            spr_draw();
        }
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (first) {
            first = 0;
            v7d854 = v7d85c;
            fade_in((unsigned char *)v7d85c);
            if (!newlap) {
                strcpy(title, "YOU HAVE THE BEST LAP TIME EVER!");
                fn_1c068();
                msgbox(title, 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
                if (newrace != -1) {
                    strcpy(title, "AND THE BEST RACE TIME EVER!");
                    fn_1c068();
                    msgbox(title, 0);
                    ticks = 0;
                    ticks2 = 0;
                    while (ticks <= 0x12c)
                        ;
                    fn_1c0ac();
                }
            } else if (newrace != -1) {
                strcpy(title, "YOU HAVE THE BEST RACE TIME EVER!");
                fn_1c068();
                msgbox(title, 0);
                ticks = 0;
                ticks2 = 0;
                while (ticks <= 0x12c)
                    ;
                fn_1c0ac();
            }
        }
        if (page_draw == 0)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        do {
            v88cec = 0;
            done = 0;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            joy88b46 = 0;
            v7d8a4 = v7d8ac;
            buttons_poll();
            if (key_select)
                done = 1;
            if (key88cd0 == 1) {
                done = 1;
                v88cec = 1;
            }
            if (key88ccd) {
                screenshot();
                if (key88ccd)
                    while (key88ccd)
                        ;
            }
            if (joy88b40 || joy88b42 || joy88b44 || joy88b46)
                done = 1;
        } while (ticks < 0x22 && !done);
    } while (!done);
    while (key88cd0 == 1)
        ;
    wait_release(0);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    page_draw = 0;
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
}

/* ---- 0x2f408, 158 bytes (no caller found): the view point for the track position curpos
 * (as at the end of 0x288xx's track step): the path vector at v7d6a0[curpos] scaled to 0x7c
 * (vec_scale), plus the camera view_x / view_y, into probe_x / probe_y and the shorts at
 * 0x6aaa6 / 0x6aaa8.
 * differs: one block: the game loads vec_kx first and parks view_y in a stack slot; operand
 * orders and a temp tried. SKIP (iter 20): ours spills vec_kx too (`movsx eax,[vec_kx];
 * mov [esp],eax; mov edx,[esp]` at 0x2f457, 2 insns/9 bytes too many) while the game loads it
 * straight into EDX; 30 spellings - both operand orders in both sums, sum and read-back statement
 * order, casts, a temp for the sum, temps for the first operands, and an `int y = view_y`
 * local - none drops the spill; every local costs a SECOND frame slot (`sub esp,8`) so the game
 * really has no locals, and the isolated 4-statement block spills the same way (`wd` in
 * .work/tp4/TW.C loads EDX fine with the second sum left out, so it is pure allocation). */
extern short w6aaa6, w6aaa8;
#pragma aux w6aaa6 "_v6aaa6"
#pragma aux w6aaa8 "_v6aaa8"

void view_at_curpos(void)
{
    v7d690 = (int *)(v7d6a0 + curpos * 8);
    vec_k = 0x7c;
    vec_x = *v7d690++;
    vec_y = *v7d690--;
    vec_scale();
    probe_x = vec_kx + view_x;
    probe_y = view_y + vec_ky;
    w6aaa6 = probe_x;
    w6aaa8 = probe_y;
}

/* ---- 0x2f4a8, 632 bytes (caller 0x3105d in the race loop): one frame's controls in a net
 * game. Reads the local controls, F1..F6 (scancodes 0x3b..0x40) give a gig 1..6 in v88ce0,
 * packs them (netkeys_pack) and swaps them with the other machine (net_key with drawing). After
 * the first two frames v7ed1c / v7ed20 get 0x2a8 / 0x16. Too many errors: the EXCESSIVE ERRORS
 * box. Player one (netgame 1) keeps its own controls and takes the peer's into key88cc3..cc7 /
 * v88cdf; player two moves its own there and takes the peer's as its kart controls, so both
 * machines drive kart 0 with player one's input. Esc (v88cec) also comes from the peer.
 * differs: 9 rows, all scheduling in the bit split (the game reads v88cef once per use, so it is
 * volatile here): the game groups four loads (0x2f60b-0x2f61d) and then the four masks, and puts
 * each in-branch read one store earlier than 9.5b does. Declaration orders, statement orders,
 * plain reads, the four masks in the game's load order (kept: 15 -> 9), `b = vv88cef; b &= m`,
 * four raw read temps (separate and comma forms) and t/u temps for the in-branch reads all tried. */
extern int v7ed90;
extern unsigned char v88cdf;
extern short w774e0;
#pragma aux w774e0 "_v774e0"
extern volatile unsigned char ukey88cd0;
#pragma aux ukey88cd0 "_key88cd0"

extern volatile unsigned char vv88cef;
#pragma aux vv88cef "_v88cef"

void net_input(void)
{
    unsigned char b2, b4, b5, b3;

    v7d8a4 = v7d8ac;
    ctl_setup(0);
    buttons_poll();
    v88cec = ukey88cd0 == 1;
    v7ed90 = ukey88cd0;
    if (v7ed90 >= 0x3b && v7ed90 <= 0x40)
        v88ce0 = v7ed90 - 0x3a;
    else
        v88ce0 = 0;
    if (v7b6d9)
        key88cd5 = 1;
    if (v7b6db)
        key88cdd = 1;
    if (v7ee4a && w774e0) {
        if (key88cd3 && !joy88b44)
            joy88b44 = 1;
        if (key88cd6 && !joy88b46)
            joy88b46 = 1;
    }
    netkeys_pack();
    v88cf3 = v88ce0;
    net88c14 = net_key(v88cf0, v88cf3, 1);
    v7ecc4++;
    if (v7ecc4 > 1) {
        v7ecc4 = 2;
        v7ed1c = 0x2a8;
        v7ed20 = 0x16;
    }
    if (net88c14) {
        fn_1c068();
        msgbox("EXCESSIVE ERRORS ABORTING!!!", 0);
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 0x12c)
            ;
        fn_1c0ac();
        return;
    }
    b5 = vv88cef & 0x20;
    b4 = vv88cef & 0x10;
    b3 = vv88cef & 8;
    b2 = vv88cef & 4;
    if (netgame == 1) {
        key88cc3 = vv88cef & 1;
        key88cc4 = b2;
        key88cc5 = b3;
        key88ccc = vv88cef & 2;
        key88cc6 = b4;
        key88cc7 = b5;
        v88cdf = v88cf2;
    } else {
        key88ccc = key88cdd;
        key88cc3 = key88cd5;
        key88cc4 = joy88b40;
        key88cc5 = joy88b42;
        key88cc6 = joy88b44;
        key88cc7 = joy88b46;
        v88cdf = v88ce0;
        key88cd5 = vv88cef & 1;
        key88cdd = vv88cef & 2;
        joy88b40 = b2;
        joy88b42 = b3;
        joy88b44 = b4;
        joy88b46 = b5;
        v88ce0 = v88cf2;
    }
    if (!v88cec)
        v88cec = vv88cef & 0x40;
}

/* ---- 0x2f720, 114 bytes (no caller found): view_box, the star field when v7eb80, then the
 * sprite list as in sprites_build; v7ed6c = 1. */
void view_sprites(void)
{
    v7ed6c = 1;
    view_box();
    if (v7eb80)
        floor_draw();
    v88bf4 = 0;
    v88b78 = 0;
    p7d86c = arr79a28;
    players_frames();
    players_project();
    ents_project();
    self_project();
    if (v88c38)
        lights_project();
    shots_project();
    if (v88b78)
        sprites_draw();
}

/* ---- 0x2f794, 1528 bytes (callers net_key 0x2de3f while it waits, the race loop 0x310c0):
 * the rest of a race frame after the first view: cycles and turn bumps, the v88c8c curtain of
 * three bars (tb80ae8: 1 slides them in to 0x72/0x80/0x8e, 2 out to 0xc7/0xc7/0xb1 setting
 * v88c9e, 3 ends), the second view in split screen (view2_in .. view2_out, with the second
 * player's controls from the keyboard: F1..F6 = scancodes 2..7 as gigs in v88cdf), the
 * overlays, animations and faces, the crack, and the Pause key (scancode 0xc5) outside net
 * games and demos: palette dimmed, music paused, PAUSED until a key. */
extern int v7d8b0, v7ed70, v7ed88, v7d5dc;
extern char v7b6e5, v7b6e7, v7b6e9, v7b6eb, v7b6ed, v7b6ef;
extern short v88c56;
extern short av7eb80[];
#pragma aux av7eb80 "_v7eb80"
void band_mirror(void);

void race_frame2(void)
{
    int u;

    v7ed74 = 1;
    cycles_step();
    bump_turn();
    if (v88c8c) {
        if (v88c8c == -1)
            v88c8c = 1;
        switch (v88c8c) {
        case 1:
            tb80ae8[0] -= 0x1e;
            if (tb80ae8[0] < 0x72)
                tb80ae8[0] = 0x72;
            tb80ae8[1] -= 0x14;
            if (tb80ae8[1] < 0x80)
                tb80ae8[1] = 0x80;
            tb80ae8[2] -= 0xa;
            if (tb80ae8[2] < 0x8e)
                tb80ae8[2] = 0x8e;
            if (tb80ae8[0] == 0x72 && tb80ae8[1] == 0x80 && tb80ae8[2] == 0x8e)
                v88c8c = 2;
            break;
        case 2:
            tb80ae8[2] += 0x1e;
            if (tb80ae8[2] > 0xb1)
                tb80ae8[2] = 0xb1;
            tb80ae8[1] += 0x14;
            if (tb80ae8[1] > 0xc7)
                tb80ae8[1] = 0xc7;
            tb80ae8[0] += 0xa;
            if (tb80ae8[0] > 0xc7)
                tb80ae8[0] = 0xc7;
            if (tb80ae8[2] == 0xb1)
                v88c9e = 1;
            if (tb80ae8[2] == 0xb1 && tb80ae8[1] == 0xc7 && tb80ae8[0] == 0xc7)
                v88c8c = 3;
            break;
        case 3:
            v88c8c = 0;
            break;
        }
    }
    players[0].pos = curpos;
    if (twoplayer || splitscreen) {
        view2_in();
        view_box();
        if (v7eb82) {
            if (v88bde)
                floor_draw_upper();
            else
                floor_draw();
        }
        v88bf4 = 0;
        v88b78 = 0;
        p7d86c = arr79a28;
        players_frames();
        players_project();
        ents_project();
        self_project();
        if (v88c38)
            lights_project();
        shots_project();
        if (v88b78)
            sprites_draw();
        bump_turn();
        v88ca2 = view_x;
        v88ca6 = view_y;
        players[1].pos = curpos;
        v7d5dc = curpos;
        view2_out();
        if (!netgame) {
            v7d8a4 = v7d8b0;
            ctl_setup(1);
            v7ed70 = (unsigned char)key88cd3;
            v7ed88 = (unsigned char)key88cd6;
            key88cc3 = key88cd5;
            key88ccc = key88cdd;
            if (v7b6e5)
                key88cc3 = 1;
            if (v7b6e7)
                key88ccc = 1;
            v7ed90 = ukey88cd0;
            if (v7ed90 >= 2 && v7ed90 <= 7)
                v88cdf = v7ed90 - 1;
            else
                v88cdf = 0;
            if (cfg7712c) {
                key88cc4 = v7b6e9;
                key88cc5 = v7b6eb;
            }
            key88cc6 = v7b6ed;
            key88cc7 = v7b6ef;
        }
    }
    overlay_draw();
    anim_tick();
    if (netvoices)
        fn_16eac();
    if (twoplayer || splitscreen) {
        spr_y = 0x2260;
        for (v88b72 = 0; v88b72 <= 1; v88b72++) {
            if (v88b72 == 1)
                spr_y -= p2offset * 0x50;
            if (cars[v88b72].f04) {
                if (av7eb80[v88b72] && v88bde)
                    band_mirror();
                u = *(int *)((char *)&cars[v88b72] + 0x7a);
                if (ticks2 - u >= 0x12c)
                    cars[v88b72].f04 = 0;
            }
        }
    }
    draw_faces();
    if (anikick)
        ani_draw();
    if (v88c42 > 1) {
        if (!v88c56) {
            v88c56 = 1;
            voice_play(0xb);
        }
        crack_draw();
    }
    if (!netgame && !v7d998 && ukey88cd0 == 0xc5) {
        save_a = page_draw;
        save_b = page_from;
        save_c = page_to;
        page_to = 0xbb80;
        page_draw = page_shown;
        page_from = page_shown;
        page_copy();
        if (musicon)
            music_pause();
        voices_reset();
        msgbox("PAUSED", 0);
        if (ukey88cd0 == 0xc5)
            while (ukey88cd0 == 0xc5)
                ;
        ukey88cd0 = 0;
        if (ukey88cd0 == 0)
            while (ukey88cd0 == 0)
                ;
        page_from = 0xbb80;
        page_draw = page_shown;
        page_to = page_shown;
        page_copy();
        page_from = save_b;
        page_to = save_c;
        page_draw = save_a;
        if (musicon)
            music_resume();
        engine_start();
    }
}

/* ---- 0x2fd8c, 10834 bytes: one race session (callers demo_mode 0x1e5d0, main_menu 0x20028 /
 * 0x202ea / 0x204fd). Sets up the karts and views, then restarts at `restart` for every try
 * (crash in split screen, lost life, menu "restart"), runs the frame loop at `loop` until the
 * race is decided, and leaves through `out`. Single-player races outside time trials (v88ca8)
 * start with the camera fly-in along the track; time trials end in trial_book; championship races add the placings to the scores
 * (placed[], 9/6/3 .. 15/12/9 points by class v88bb2) and go to race_results; a kart that fails
 * to qualify (place > 3) loses a life (f6c) and gets the TRY AGAIN menu. Demo record (v7d998 1)
 * and playback (2, 3; v88c8e picks the shown record) are inlined (demo_rec, demo_play). Esc
 * gives the in-race menu (BACK TO RACE / LEAVE RACE / SAVE AND QUIT / RESTART RACE).
 * differs (120 rows, ours 10808 bytes vs the game's 10834): the EAX/EDX roles of the temporaries
 * are the mirror image of the game's in the repeated blocks - the path-vector walk (`imul eax,eax,8`
 * + `add edx,eax` 0x3066d-0x30682, ours `lea edx,[eax*8]` + `add eax,edx`) and the camera sums
 * (`movsx edx,[v88ca2]` / `movsx eax,[vec_kx]` / `add edx,eax` 0x306af-0x306bd) - plus a few store
 * orders and load placements. What fixed 128 -> 120: `short placed[10]` sized from the frame (the
 * game's array base is [esp+0x28] at 0x30192 and its two scalar slots are [esp+0x3c]/[esp+0x40], so
 * 0x44 - 0x28 - 8 = 20 bytes; the largest index used is v88b72 <= 7). Tried at all 4 walk sites
 * (probe .work/probe_rr/, control 136): product operand order swapped, `curpos << 3`, a `k` local
 * for the product, the pointer walk spelled as four statements - all equal the control. */
#pragma pack(1)
struct rsave {
    int myplayer, other, laps, cls, metal, track;
    int f1e[8];
    int seat[8];
    int f5a0, face0, hits0, f5a1, face1, hits1;
    int pad;
};
#pragma pack()
extern struct rsave rsave774e8[2];
#pragma aux rsave774e8 "_arr774e8"
extern int iv7ecb0;
#pragma aux iv7ecb0 "_v7ecb0"
extern short v88ca8, v88b62, v88b64, w774e2;
#pragma aux w774e2 "_v774e2"
extern int v7ed18, v7ed58, v7ed54, v7ed50, v7ea64, v7ea6c, v7ec4c, v7ebac;
extern int ctl775cc, ctl77558, ctl774e4;
extern short ctl775d8, ctl775dc, ctl775d6, ctl775da;
extern char key88cd1, v88ce1;
extern short v88c92, v88c94;
extern int v7c654;
extern unsigned char v7b6e1, v7b6e3;
void horizon_upload(void);
void f29e0c(void);
void track_setup_x(void);
#pragma aux track_setup_x "track_setup_"
void track_here_x(void);
#pragma aux track_here_x "track_here_"
void netkeys_in_x(void);
#pragma aux netkeys_in_x "netkeys_in_"

void race_run(void)
{
    char buf[0x28];
    short placed[10];           /* [esp+0x28]; frame 0x44 with two slots from 0x3c (0x2fe4d) */
    int oldclock, first, go, crash, saverace;
    short i, sel;
    struct surf *sp;
    int j, k;

    v7ec88 = 0x4b0;
    racetime = 0;
    v7ec84 = 0;
    v88ca8 = 0;
    v7edb4 = 0;
    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x440;
    if (!netvoices && !twoplayer && !splitscreen && v88bb2 != 4)
        v88ca8 = 1;
    if (v88ca0 && (v88bde || v7d998 == 2 || twoplayer || netvoices))
        v88ca0 = 0;
    first = 1;
    kart7d5c0 = karts;
    kart7d5bc = &karts[1];
    curkart = kart7d5c0;
    curkart->f6c = 3;
    curkart++;
    curkart->f6c = 3;
    fn_16ca0();
    nplayers = 7;
    if (twoplayer)
        nplayers = 1;
    if (splitscreen) {
        if (splitscreen == 2)
            nplayers = 7;
        else
            nplayers = 1;
    }
    if (!twoplayer && !v7d998 && v88bb2 == 4)
        nplayers = 0;
    if (netvoices)
        nplayers = 7;
    if (twoplayer || netvoices)
        music_start(v7eb00, 1);
    if (!twoplayer && !netvoices && !netgame && v88bb2 != 4) {
        k = splitscreen != 0;
        rsave774e8[k].myplayer = myplayer;
        rsave774e8[k].other = v88c6a;
        rsave774e8[k].laps = nlaps;
        rsave774e8[k].cls = v88bb2;
        rsave774e8[k].metal = v88bb6;
        rsave774e8[k].track = v88b88;
        for (j = 0; j < 8; j++) {
            rsave774e8[k].f1e[j] = players[j].f1e;
            rsave774e8[k].seat[j] = v70720[j];
        }
        rsave774e8[k].f5a0 = cars[0].f5a;
        rsave774e8[k].face0 = cars[0].face;
        rsave774e8[k].hits0 = cars[0].fc0;
        rsave774e8[k].f5a1 = cars[1].f5a;
        rsave774e8[k].face1 = cars[1].face;
        rsave774e8[k].hits1 = cars[1].fc0;
    }
restart:
    if (v7d998) {
        for (i = 0; i <= 1; i++) {
            cars[i].f5a = 0;
            cars[i].face = 0;
        }
    }
    if (v88ca8) {
        oldclock = optclock;
        optclock = 0;
    }
    v7eccc = 0;
    fn_430f7(laptimes, 0, 6);
    anikick = 0;
    anitimer = 0;
    voices_reset();
    v88c56 = 0;
    v88c48 = 0;
    shots_clear();
    karts[0].f56 = 0;
    karts[1].f56 = 0;
    v88c9e = 0;
    v88c8c = 0;
    v88b7e = 0;
    curkart = kart7d5c0;
    if (!twoplayer && !netvoices) {
        v88c38 = 1;
        v88c5e = 0;
        v88c44 = 0;
    } else {
        v88c38 = 0;
        v88c44 = 2;
    }
    for (v88b72 = 0; v88b72 <= 7; v88b72++)
        placed[v88b72] = 0;
    curkart = kart7d5c0;
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        ents_flags();
        curkart++;
    }
    finished = 0;
    curkart = kart7d5c0;
    ticks = 0;
    ticks2 = 0;
    v88c3c = 0;
    v88c42 = 0;
    track_setup_x();
    if (nplayers > 1) {
        karts[0].fac = 8;
        karts[1].fac = 8;
    } else {
        karts[0].fac = 1;
        karts[1].fac = 1;
    }
    for (v88b72 = 0; v88b72 <= 0x17; v88b72++) {
        if (twoplayer || splitscreen || netvoices) {
            cars[v88b72].f5a = 0;
            cars[v88b72].face = 0;
        }
        karts[v88b72].f86 = -1;
        karts[v88b72].f2e = 0;
        *(int *)&karts[v88b72].f2a = 0;
        karts[v88b72].fa2 = 0;
        karts[v88b72].fb4 = 0;
        karts[v88b72].fb2 = 0;
        karts[v88b72].fe4 = 0;
        karts[v88b72].fb0 = 0;
        karts[v88b72].fa6 = 0;
        karts[v88b72].fa8 = 0;
        karts[v88b72].fa4 = 0;
        karts[v88b72].faa = 0;
        karts[v88b72].fb8 = 0;
        karts[v88b72].fbc = 0;
        karts[v88b72].fc0 = 0;
        karts[v88b72].fdc = 0;
        karts[v88b72].f100 = 0;
        karts[v88b72].fe8 = 0;
        karts[v88b72].fec = 0;
        karts[v88b72].f8e = 0;
        karts[v88b72].f06 = 0;
        karts[v88b72].f7e = 0;
        karts[v88b72].f28 = 0;
        karts[v88b72].f80 = 0;
        karts[v88b72].f7c = 0;
        karts[v88b72].f44 = 0;
        karts[v88b72].f5a = 0;
        karts[v88b72].f5c = 0;
        karts[v88b72].f5e = 0;
        karts[v88b72].f46 = 0;
        karts[v88b72].f48 = 0;
        karts[v88b72].f4a = sparkdata;
        karts[v88b72].f0a = 0;
    }
    iv7ecb0 = 7;
    v7eca4 = 0;
    fn_19db8();
    fn_16f90();
    curpos = 0x1e0;
    v88b62 = 0;
    v88b64 = 0;
    curkart = kart7d5c0;
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        v88b7e = v88b72;
        curkart->f1a = 2;
        curkart->f00 = 0;
        curkart->f18 = 0;
        curkart->f1c = 2;
        curkart->f4e = tb7b518[curkart->f1a] + v88b7e;
        curkart++;
    }
    v88b7e = 0;
    curkart = kart7d5c0;
    v88b3a = 0;
    v88b38 = 0;
    v88b50 = 0;
    v7d690 = (int *)(v7d6a0 + curpos * 8);
    vec_x = *v7d690++;
    vec_y = *v7d690--;
    curkart = kart7d5c0;
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        curkart->f68 = 0;
        curkart->f22 = 0;
        curkart->f24 = 0;
        curkart->f3a = 0;
        curkart->f36 = 0;
        curkart->f3c = 0;
        curkart->f20 = 0;
        curkart->f26 = 0;
        curkart->f54 = 0;
        curkart->f52 = 0;
        curkart->f42 = 0;
        curkart->f26 = tbl7e730[curkart->f26];
        curkart++;
    }
    curkart = kart7d5c0;
    view_x = players[0].x;
    v88c4a = view_x + 0x28;
    v88c4c = players[0].y - 10;
    v88ca6 = players[1].y + 0x7c;
    v88b92 = 0;
    v88ca2 = players[1].x;
    v88b90 = 0;
    view_y = players[0].y - 0x7c;
    curpos = 0x1e0;
    if (splitscreen)
        v7d5dc = 0x1e0;
    else
        v7d5dc = 0x5a0;
    players[0].pos = curpos;
    players[1].pos = v7d5dc;
    curpos = players[0].pos;
    track_here_x();
    vec_k = 0x7c;
    vec_scale();
    probe_x = view_x + vec_kx;
    probe_y = view_y + vec_ky;
    players[0].x = probe_x;
    players[0].y = probe_y;
    if (splitscreen) {
        v88ca2 = view_x + 0x14;
        v88ca6 = view_y;
    }
    if (twoplayer || splitscreen) {
        curpos = players[1].pos;
        v7d690 = (int *)(v7d6a0 + curpos * 8);
        vec_k = 0x7c;
        vec_x = *v7d690++;
        vec_y = *v7d690--;
        vec_scale();
        probe_x = v88ca2 + vec_kx;
        probe_y = v88ca6 + vec_ky;
        players[1].x = probe_x;
        players[1].y = probe_y;
        curpos = players[0].pos;
    }
    if (v7d998 && first && v7d998 != 3) {
        palptr = pal7d850;
        getpalette();
        fade_out(pal7d850);
        page_draw = 0;
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
    }
    if (v88ca8) {
        v88c92 = view_x + 0x84;
        v88c94 = view_y;
        curpos = 0x3c0;
        view_x = 0x708;
        view_y = v88c94 + 0x84;
        if (v7d998 != 2)
            music_start(v7eb00, 1);
    }
    for (v88b72 = 0; v88b72 <= 1; v88b72++)
        if ((karts[v88b72].fe0 & 4) || (karts[v88b72].fe0 & 0x10) || (karts[v88b72].fe0 & 8))
            cars[v88b72].f5a = 0x63;
    frame_draw();
    if (v7d998 && first) {
        first = 0;
        fade_in((unsigned char *)p7d718);
    }
    if (v88ca8) {
        go = 1;
        for (;;) {
            do {
                v7e724 = ticks;
                v88c44 = 0;
                v7eba8 = 0;
                view_box();
                floor_draw();
                p7d86c = arr79a28;
                v88bf4 = 0;
                v88b78 = 0;
                players_frames();
                players_project();
                ents_project();
                self_project();
                if (v88c38)
                    lights_project();
                shots_project();
                if (v88b78)
                    sprites_draw();
                cycles_step();
                overlay_draw();
                anim_tick();
                draw_faces();
                page_show();
                if (key88ccd) {
                    screenshot();
                    if (key88ccd)
                        while (key88ccd)
                            ;
                }
                if (page_draw)
                    page_draw = 0;
                else
                    page_draw = 0x3e80;
                if (v7ea5c)
                    while (ticks - v7e724 <= v7ea5c)
                        ;
                if (go == 1) {
                    view_x -= 0x40;
                    if (view_x <= v88c92) {
                        view_x = v88c92;
                        go = 0;
                    }
                }
            } while (go);
            path_pt_scale();
            v7d798 = probe_x;
            v7d79c = probe_y;
            curkart->f52++;
            if (curkart->f52 > 4)
                curkart->f52 = 4;
            curpos -= 0x28;
            if (curpos <= 0x1e0)
                break;
            path_pt_scale();
            v7d768 = v7d798 - probe_x;
            v7d76c = v7d79c - probe_y;
            view_x += v7d768;
            view_y += v7d76c;
            f24834(0);
        }
        curkart->f52 = 0;
        view_x = v88c92 - 0x84;
        view_y = v88c94;
        curpos = 0x1e0;
        optclock = oldclock;
    }
    joy88b40 = 0;
    key88cd1 = 0;
    v88ce1 = 0;
    v88ce0 = 0;
    v88cdf = 0;
    if (twoplayer || splitscreen) {
        curkart = kart7d5bc;
        curpos = v7d5dc;
        v7d678 = curpos;
        v7d690 = (int *)(v7d6a0 + curpos * 8);
        vec_k = 0x7c;
        vec_x = *v7d690++;
        vec_y = *v7d690--;
        vec_scale();
        probe_x = v88ca2 + vec_kx;
        probe_y = v88ca6 + vec_ky;
        surface_at();
        v88b68 = probe_x;
        v88b6a = probe_y;
        curkart->fae = map_at(v88b68, v88b6a);
        curkart->f36 = surfs[surf_class].f120;
        curkart->f3a = surfs[surf_class].f124;
        curkart->f6e = -1;
        sp = &surfs[surf_class];
        curkart->f76 = sp;
        curkart->f72 = sp;
        curkart->f56 = surf_class;
        v7d89c = sp;
        curkart = kart7d5c0;
    }
    if (twoplayer || splitscreen || netvoices)
        curpos = 0x1e0;
    v7d678 = curpos;
    v7d690 = (int *)(v7d6a0 + curpos * 8);
    vec_k = 0x7c;
    vec_x = *v7d690++;
    vec_y = *v7d690--;
    vec_scale();
    probe_x = view_x + vec_kx;
    probe_y = view_y + vec_ky;
    surface_at();
    v88b68 = probe_x;
    v88b6a = probe_y;
    curkart->fae = map_at(v88b68, v88b6a);
    curkart->f36 = surfs[surf_class].f120;
    curkart->f3a = surfs[surf_class].f124;
    curkart->f6e = -1;
    sp = &surfs[surf_class];
    curkart->f76 = sp;
    v7d89c = sp;
    curkart->f72 = sp;
    v7ed18 = surf_class;
    curkart->f56 = surf_class;
    if (!netvoices) {
        for (v88b72 = 0; v88b72 <= nplayers; v88b72++) {
            if (v88b72) {
                players[v88b72].cell = map_at(players[v88b72].x, players[v88b72].y);
                players[v88b72].f0a = tbl7e730[players[v88b72].f0a];
                players[v88b72].f08 = tbl7e730[players[v88b72].f08];
                if (curkart->f36) {
                    players[v88b72].f0a -= curkart->f36 * players[v88b72].f0a >> 16;
                    players[v88b72].f08 -= curkart->f36 * players[v88b72].f08 >> 16;
                }
            }
        }
    }
    for (v88b72 = 0; v88b72 <= 1; v88b72++)
        karts[v88b72].fb2 = 0;
    curkart = kart7d5c0;
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        v88b7e = v88b72;
        curkart->f00 = 0;
        curkart->f1a = 2;
        curkart->f18 = 0;
        curkart->f1c = 2;
        curkart->f4e = tb7b518[curkart->f1a] + v88b7e;
        curkart++;
    }
    v88b7e = 0;
    curkart = kart7d5c0;
    ticks = 0;
    ticks2 = 0;
    if (v88c38) {
        v88c44 = 1;
        v7eba8 = 0;
    }
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        v88b7e = v88b72;
        curkart->f00 = 0;
        curkart->f1a = 2;
        curkart->f18 = 0;
        curkart->f1c = 2;
        curkart->f4e = tb7b518[curkart->f1a] + v88b7e;
        curkart++;
    }
    v88b7e = 0;
    curkart = kart7d5c0;
    if (!v7d998) {
        if (!twoplayer && !v88ca8 && !netvoices)
            music_start(v7eb00, 1);
    } else {
        demo_start();
        if (v7d998 == 2 && cursong != v7c654)
            music_start(v7c654, 1);
    }
    if (netgame)
        v7ea5c = 0xc;
    for (k = 0; k < 2; k++)
        cars[k].sndhandle = -1;
    engine_start();
    v88ca8 = 0;
    ticks = 0;
    racetime = 0;
    overruns = 0;
loop:
    v7ed6c = 0;
    v7ed74 = 0;
    if (v88c44 != 2)
        racetime = 0;
    v7e724 = ticks;
    if (netgame) {
        net_input();
        if (net88c14)
            goto out;
    } else if (key88cd0 == 1)
        v88cec = 1;
    else
        v88cec = 0;
    if (!v7ed6c) {
        v7ed6c = 1;
        view_box();
        if (v7eb80)
            floor_draw();
        sprites_build_x();
    }
    if (!v7ed74)
        race_frame2();
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    if (page_draw)
        page_draw = 0;
    else
        page_draw = 0x3e80;
    if (v7ea5c)
        while (ticks - v7e724 < v7ea5c)
            ;
    if (v7ec84 && page_draw == 0x3e80) {
        voices_reset();
        duck_screen(1);
        goto out;
    }
    ticks2++;
    if (!netgame) {
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (twoplayer || splitscreen) {
            v7ed90 = ukey88cd0;
            if (v7ed90 >= 0x3b && v7ed90 <= 0x40)
                v88ce0 = v7ed90 - 0x3a;
            else
                v88ce0 = 0;
            if (!netgame) {
                if (!cfg7712c) {
                    key88cc4 = joy88b44;
                    key88cc5 = joy88b46;
                } else if (cfg77128w)
                    goto keys;
                joy88b44 = (unsigned char)v7b6e1;
                joy88b46 = (unsigned char)v7b6e3;
            }
        }
    keys:
        if (v7b6d9)
            key88cd5 = 1;
        if (v7b6db)
            key88cdd = 1;
    }
    if ((twoplayer || splitscreen) && page_draw == 0x3e80) {
        crash = 0;
        if (karts[0].f7e || karts[1].f7e) {
            if (karts[0].f7e && karts[1].f7e) {
                if (splitscreen)
                    strcpy(buf, "WHAT A CRASH!");
                else
                    strcpy(buf, "THAT WAS A DOUBLE KILL!");
                if (players[0].f56 > 0xc && players[1].f56 > 0xc)
                    crash = 1;
            } else {
                if (karts[0].f7e && players[0].f56 > 0xc) {
                    strcpy(buf, pnames[myplayer]);
                    strcat(buf, " HAS BEEN WAXED!");
                    crash = 1;
                }
                if (karts[1].f7e && players[1].f56 > 0xc) {
                    strcpy(buf, pnames[v88c6a]);
                    strcat(buf, " HAS BEEN WAXED!");
                    crash = 1;
                }
            }
            if (crash) {
                page_draw = 0;
                msgbox(buf, 0);
                ticks = 0;
                while (ticks <= 0x190)
                    ;
                ticks = 0;
                ticks2 = 0;
                page_draw = 0x3e80;
                if (splitscreen)
                    goto restart;
                if (karts[0].f7e)
                    cars[1].fc0++;
                if (karts[1].f7e)
                    cars[0].fc0++;
                if (cars[0].fc0 >= hits_target || cars[1].fc0 >= hits_target)
                    crash = 2;
                if (crash == 2) {
                    voices_reset();
                    overall_winner();
                    for (k = 0; k < 2; k++)
                        cars[k].fc0 = 0;
                    goto out;
                }
                karts[0].f6c = 3;
                karts[1].f6c = 3;
                goto restart;
            }
        }
    }
    if (v88c42 == 4 && (j = v7e72c, ticks2 - j) >= 0xd && page_draw == 0x3e80) {
        page_from = 0;
        page_to = 0x3e80;
        page_copy();
        if (!v7d998)
            curkart->f6c--;
        f29e0c();
        if (!curkart->f6c)
            goto out;
        goto restart;
    }
    if (splitscreen && (karts[0].fb2 == nlaps + 1 || karts[1].fb2 == nlaps + 1)) {
        if (v88b88 == 5)
            w88c08 = 0;
        else
            w88c08 = 3;
        for (v88b72 = 0; v88b72 <= 0x2ff; v88b72++)
            v7d858[v88b72] = p7d718[v88b72];
        v7d854 = v7d858;
        voices_reset();
        for (v88b72 = 0; v88b72 <= nplayers; v88b72++)
            players[v88b72].f1e += karts[v88b72].fac + 9 - 2 * karts[v88b72].fac;
        v7ec80 = 0;
        race_results();
        return;
    }
    if (nlaps + 1 == curkart->fb2 && !v88c3c && !v88c42 && !splitscreen) {
        if (v88bb2 == 4) {
            v7eccc = 0;
            for (v88c3c = 0; v88c3c <= 5; v88c3c++)
                v7eccc += laptimes[v88c3c];
        } else
            v7eccc = racetime;
        players[v88b7e].f5c = 0;
        curkart->f44 = 0;
        curkart->f24 = 0;
        curkart->f22 = 0;
        curkart->f3c = 0;
        curkart->f20 = 0;
        v88c3c = 1;
        curkart->fb4 = 0;
        v7ea64 = ticks;
        if ((short)players[0].place >= 1 && (short)players[0].place <= 3) {
            v88c5c = 0;
            v88c36 = 0xa;
            curkart->f4e = (int *)lit7b378;
            v88c9c = 0;
            voice_play(0x11);
        } else {
            v88c5a = 0;
            v88c36 = 0xd;
            curkart->f4e = (int *)lit7b238;
        }
        curkart->f18 = v88c36;
        v88c3a = 0;
        curkart->f32 = ticks2;
    }
    if (v88c3c) {
        switch (v88bb2) {
        case 1:
            v7ed58 = 9;
            v7ed54 = 6;
            v7ed50 = 3;
            break;
        case 2:
            v7ed58 = 0xc;
            v7ed54 = 9;
            v7ed50 = 6;
            break;
        case 3:
            v7ed58 = 0xf;
            v7ed54 = 0xc;
            v7ed50 = 9;
            break;
        }
        if (finished >= 3 || v88bb2 == 4) {
            if (v88c3c != 2) {
                for (v88b72 = 0; v88b72 <= 7; v88b72++) {
                    switch (players[v88b72].place) {
                    case 1:
                        placed[v88b72] = v7ed58;
                        break;
                    case 2:
                        placed[v88b72] = v7ed54;
                        break;
                    case 3:
                        placed[v88b72] = v7ed50;
                        break;
                    }
                }
                if ((short)players[0].place >= 1 && (short)players[0].place <= 3)
                    result_msg();
                v7ea6c = ticks;
                v88c3c = 2;
            } else if ((j = v7ea6c, ticks - j) >= 0x320 && page_draw == 0x3e80) {
                if ((short)players[0].place > 3) {
                    w88c08 = 0;
                    curkart->f6c--;
                    f29e0c();
                    if (curkart->f6c || (v88b88 != 5 && !v7ec80)) {
                        if (!curkart->f6c)
                            goto out;
                        page_from = 0;
                        page_to = 0x3e80;
                        page_copy();
                        ticks = 0;
                        ticks2 = 0;
                        w88c98 = 0;
                        if (!netgame && v88bb2 != 4 && !v7edb0 && !v7ec80) {
                            strcpy(mrows[2], " LEAVE RACE     ");
                            strcpy(mrows[3], " SAVE AND QUIT  ");
                            w88c98 = 1;
                        } else
                            strcpy(mrows[2], " LEAVE RACE     ");
                        if (v88b88 != 5)
                            strcpy(mnames, " TRY TO QUALIFY ");
                        else
                            strcpy(mnames, " TRY AGAIN      ");
                        if (w88c98) {
                            sel = menu(3, 0, 1, 0);
                            v7ecc4 = 2;
                            key88cc3 = 0;
                            key88ccc = 0;
                            key88cc4 = 0;
                            key88cc5 = 0;
                            key88cc6 = 0;
                            key88cc7 = 0;
                            v88cdf = 0;
                            v88ce0 = 0;
                            key88cd5 = 0;
                            key88cdd = 0;
                            key88cd6 = 0;
                            key88cd3 = 0;
                            joy88b40 = 0;
                            joy88b42 = 0;
                            joy88b44 = 0;
                            joy88b46 = 0;
                            v88cec = 0;
                            if (sel == 3) {
                                if (splitscreen) {
                                    ctl775cc = splitscreen;
                                    ctl775d8 = v7ee5a;
                                    ctl77558 = 1;
                                    ctl775dc = v7ecb4;
                                } else {
                                    ctl775d6 = v7ee5a;
                                    ctl774e4 = 1;
                                    ctl775da = v7ecb4;
                                }
                                save_config(0);
                                return;
                            }
                            w88c98 = 0;
                        } else {
                            sel = menu(2, 0, 1, 0);
                            v7ecc4 = 2;
                            key88cc3 = 0;
                            key88ccc = 0;
                            key88cc4 = 0;
                            key88cc5 = 0;
                            key88cc6 = 0;
                            key88cc7 = 0;
                            key88cd5 = 0;
                            key88cdd = 0;
                            key88cd6 = 0;
                            key88cd3 = 0;
                            joy88b40 = 0;
                            joy88b42 = 0;
                            joy88b44 = 0;
                            joy88b46 = 0;
                            v88ce0 = 0;
                            v88cdf = 0;
                            v88cec = 0;
                        }
                        sprptr = p6a980;
                        spr_h = v88c00;
                        spr_w = v88bee;
                        page_draw = 0;
                        spr_x = v88bf0;
                        sprptr += 0x6400;
                        spr_y = v88bf2;
                        spr_draw_opaque();
                        page_show();
                        if (key88ccd) {
                            screenshot();
                            if (key88ccd)
                                while (key88ccd)
                                    ;
                        }
                        page_from = 0;
                        page_to = 0x3e80;
                        page_copy();
                        page_draw = 0x3e80;
                        if (!v7d998 || v7d998 == 3) {
                            v7eb54 = p6a980;
                            horizon_upload();
                        }
                        if (sel == 2)
                            goto out;
                        goto restart;
                    }
                }
                for (v88b72 = 0; v88b72 <= 7; v88b72++)
                    players[v88b72].f1e += placed[v88b72];
                page_from = 0;
                v7ed5c = players[0].f1e;
                page_to = 0x3e80;
                page_copy();
                page_draw = 0;
                page_show();
                if (key88ccd) {
                    screenshot();
                    if (key88ccd)
                        while (key88ccd)
                            ;
                }
                page_draw = 0x3e80;
                voices_reset();
                if (v88bb2 != 4) {
                    if (v7ec80) {
                        v88b88 = 5;
                        v7ec80 = 2;
                    }
                    if (v88b88 == 5)
                        w88c08 = 0;
                    else
                        w88c08 = 3;
                    for (v88b72 = 0; v88b72 <= 0x2ff; v88b72++)
                        v7d858[v88b72] = p7d718[v88b72];
                    v7d854 = v7d858;
                    race_results();
                    if (v7ec80 == 2)
                        v7ec80 = 0;
                    if (!v7ec80)
                        goto out;
                    v88b88 = 4;
                    w88c08 = 3;
                    return;
                }
                if (twoplayer || splitscreen || v7d998 || netvoices)
                    goto out;
                v88cec = 0;
                trial_book(v88bb6 + v7ecb4, v88b88, 1);
                v88cec = 0;
                return;
            }
        }
    }
    if (v7d998) {
        if (v7d998 == 1) {
            netkeys_pack();
            v88cca = v88cf0;
            fwrite(&v88cca, 1, 1, v7eb68);
            if (v88cec) {
                v7d998 = 0;
                fclose(v7eb68);
                return;
            }
        } else {
            if (ukey88cd0 == 1) {
                v7ec68 = 1;
                return;
            }
            go = 0;
            if (v7d998 == 2) {
                if (key_down || key88cd3 || key_select || joy88b40 || key88cdd) {
                    if (v88c8e < w88c9a) {
                        go = 1;
                        v88c8e++;
                    }
                }
                if (key_up || key88cd6 || key88cd5) {
                    if (v88c8e > 1) {
                        go = 1;
                        v88c8e--;
                    }
                }
                if (go) {
                    pcx_load((char *)&rec7c208[v88c8e], 1);
                    v88b4a = 0;
                    sel = page_draw;
                    page_draw = 0x7d00;
                    p7d714 = p6a97c;
                    pcx_blit();
                    page_draw = sel;
                }
            }
            v88cef = demo7d9a0[v7d994].k;
            v88cca = v88cef;
            netkeys_in_x();
            if (--v7e564 <= 0) {
                v7d994++;
                v7e564 = demo7d9a0[v7d994].n;
            }
            if (v88cec) {
                if (v7d998 == 3)
                    goto out;
                if (page_draw == 0x3e80)
                    goto restart;
                page_from = 0x3e80;
                page_to = 0;
                page_copy();
                page_show();
                if (key88ccd) {
                    screenshot();
                    if (key88ccd)
                        while (key88ccd)
                            ;
                }
                page_draw = 0x3e80;
                goto restart;
            }
        }
    }
    if (!v7d998 && !netgame) {
        if (v7ee4a && w774e0) {
            if (key88cd3 && !joy88b44)
                joy88b44 = 1;
            if (key88cd6 && !joy88b46)
                joy88b46 = 1;
        }
        if ((twoplayer || (splitscreen && !netgame)) && w774e2 && !cfg7712c) {
            if (v7ed70 && !key88cc6)
                key88cc6 = 1;
            if (v7ed88 && !key88cc7)
                key88cc7 = 1;
        }
    }
    if (v88cec && !v88c3c && !curkart->f7e && !v88c42 && page_draw == 0x3e80) {
        w88c08 = 0;
        page_from = 0;
        page_to = 0x3e80;
        page_copy();
        saverace = racetime;
        v7ec4c = ticks;
        v7ebac = ticks2;
        ticks = 0;
        ticks2 = 0;
        w88c98 = 0;
        if (!twoplayer && !netvoices) {
            strcpy(mnames, " BACK TO RACE  ");
            if (!netgame && v88bb2 != 4 && !v7edb0 && !v7ec80) {
                strcpy(mrows[2], " LEAVE RACE    ");
                strcpy(mrows[3], " SAVE AND QUIT ");
                w88c98 = 1;
            } else
                strcpy(mrows[2], " LEAVE RACE    ");
        } else {
            strcpy(mnames, " BACK TO ZONE   ");
            strcpy(mrows[2], " LEAVE ZONE     ");
        }
        if (w88c98)
            strcpy(mrows[4], " RESTART RACE  ");
        else
            strcpy(mrows[3], " RESTART RACE  ");
        if (!twoplayer && !netvoices) {
            v88b72 = (curkart->f6c != 1) + 2;
            if (w88c98) {
                if (curkart->f6c == 1)
                    v88b72 = 3;
                else
                    v88b72 = 4;
            }
        } else
            v88b72 = 2;
        sel = menu(v88b72, 0, 1, 0);
        v7ecc4 = 2;
        if (netgame && net88c14) {
            fn_1c068();
            msgbox("EXCESSIVE ERRORS!", 0);
            ticks = 0;
            ticks2 = 0;
            while (ticks <= 0x12c)
                ;
            fn_1c0ac();
            return;
        }
        key88cd5 = 0;
        key88cdd = 0;
        key88cd6 = 0;
        key88cd3 = 0;
        joy88b40 = 0;
        joy88b42 = 0;
        joy88b44 = 0;
        joy88b46 = 0;
        key88cc3 = 0;
        key88ccc = 0;
        key88cc4 = 0;
        key88cc5 = 0;
        key88cc6 = 0;
        key88cc7 = 0;
        sprptr = p6a980;
        v88ce0 = 0;
        spr_h = v88c00;
        v88cdf = 0;
        spr_w = v88bee;
        v88cec = 0;
        page_draw = 0;
        spr_x = v88bf0;
        sprptr += 0x6400;
        spr_y = v88bf2;
        spr_draw_opaque();
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        page_from = 0;
        page_to = 0x3e80;
        page_copy();
        page_draw = 0x3e80;
        if (!v7d998 || v7d998 == 3) {
            v7eb54 = p6a980;
            horizon_upload();
        }
        if (w88c98) {
            if (sel == 3) {
                if (splitscreen) {
                    ctl775cc = splitscreen;
                    ctl775d8 = v7ee5a;
                    ctl77558 = 1;
                    ctl775dc = v7ecb4;
                } else {
                    ctl775d6 = v7ee5a;
                    ctl774e4 = 1;
                    ctl775da = v7ecb4;
                }
                save_config(0);
                return;
            }
            w88c98 = 0;
        }
        if (sel == 2)
            goto out;
        if (sel == 3 || sel == 4) {
            if (!twoplayer && !splitscreen && !netvoices)
                curkart->f6c--;
            f29e0c();
            if (!curkart->f6c)
                goto out;
            goto restart;
        }
        ticks = v7ec4c;
        ticks2 = v7ebac;
        racetime = saverace;
        engine_start();
    }
    kart_buttons();
    kart_frame();
    players[0].x = v88b68;
    players[0].y = v88b6a;
    players[0].pos = curpos;
    kart_fire();
    if (twoplayer || splitscreen) {
        view2_in();
        v88ce0 = v88cdf;
        key88cd5 = key88cc3;
        key88cdd = key88ccc;
        joy88b40 = (unsigned char)key88cc4;
        joy88b42 = (unsigned char)key88cc5;
        joy88b44 = (unsigned char)key88cc6;
        joy88b46 = (unsigned char)key88cc7;
        kart_buttons();
        kart_frame();
        players[1].x = v88b68;
        players[1].y = v88b6a;
        view2_save();
        kart_fire();
        view2_out();
        if (splitscreen && v88c44 == 2) {
            cpu_drive();
            car_laps();
        }
    } else if (v88c44 == 2) {
        cpu_drive();
        if (!netvoices)
            car_laps();
    }
    shots_move();
    goto loop;
out:
    ;
}

/* ---- 0x327b4, 41 bytes (no caller found): resets four scale constants of the asm renderer
 * (0x80, 0x80, 0x1000, 0x1000). */
extern int v7d648, v7d644, v7d664, v7d634;

void render_scales(void)
{
    v7d648 = 0x80;
    v7d644 = 0x80;
    v7d664 = 0x1000;
    v7d634 = 0x1000;
}

/* ---- 0x327e0, 241 bytes (caller main_menu 0x2028c, before the race loop): picks the first two
 * players other than myplayer into v7ec70 and v7ec6c, then loads DIGGER.SP (0x2140 bytes each,
 * 8 frames of 0x428) over their CARS.SP slots in v7d708 (stride 0x31e0) and points faces[].img[0..7]
 * at them. f328d4 (after the loop) reloads CARS.SP. What the mode is: LIKELY a digger race;
 * evidence is only the file name. */
extern int iv7ec70, iv7ec6c;
#pragma aux iv7ec70 "_v7ec70"
#pragma aux iv7ec6c "_v7ec6c"

void digger_load(void)
{
    int i;
    char *p;

    for (i = 0; i <= 7; i++)
        if (i != myplayer) {
            iv7ec70 = i;
            break;
        }
    for (i = 0; i <= 7; i++)
        if (i != myplayer && i != iv7ec70) {
            iv7ec6c = i;
            break;
        }
    p = v7d708 + iv7ec70 * 0x31e0;
    datfind("DIGGER.SP");
    fread(p, 0x2140, 1, datfile);
    for (i = 0; i < 8; i++) {
        faces[iv7ec70].img[i] = p;
        p += 0x428;
    }
    p = v7d708 + iv7ec6c * 0x31e0;
    fread(p, 0x2140, 1, datfile);
    for (i = 0; i < 8; i++) {
        faces[iv7ec6c].img[i] = p;
        p += 0x428;
    }
}

/* ---- 0x328d4, 573 bytes (caller main_menu 0x202bc, after the digger loop reloaded CARS.SP): puts
 * the eight player sprite file names back into arr7bba0 (PANDA, CAMEL, MOOSE, PELICAN, SHARK,
 * RINGO, ELE, TIGER), points faces[i].img[0..11] at consecutive 0x428-byte frames of v7d708, sets
 * each face's two frame counts f90/f92 (4/2, 4/4, 4/8, 4/8, 2/2, 1/7, 2/2, 1/4), its record size
 * f94 = (f90 + f92 + 12) * 0x428, and img[12..23] plus f90 frames at img[24] and f92 at img[28]
 * from v7d6c8 (the same base for every face). Undoes digger_load. */
void faces_reset(void)
{
    int i, j;
    char *p;

    strcpy(arr7bba0, "PANDA.SP");
    strcpy(arr7bba0 + 0xc, "CAMEL.SP");
    strcpy(arr7bba0 + 0x18, "MOOSE.SP");
    strcpy(arr7bba0 + 0x24, "PELICAN.SP");
    strcpy(arr7bba0 + 0x30, "SHARK.SP");
    strcpy(arr7bba0 + 0x3c, "RINGO.SP");
    strcpy(arr7bba0 + 0x48, "ELE.SP");
    strcpy(arr7bba0 + 0x54, "TIGER.SP");
    p = v7d708;
    for (i = 0; i <= 7; i++) {
        for (j = 0; j < 8; j++) {
            faces[i].img[j] = p;
            p += 0x428;
        }
        for (j = 0; j < 4; j++) {
            faces[i].img[8 + j] = p;
            p += 0x428;
        }
    }
    faces[0].f90 = 4;
    faces[0].f92 = 2;
    faces[1].f90 = 4;
    faces[1].f92 = 4;
    faces[2].f90 = 4;
    faces[2].f92 = 8;
    faces[3].f90 = 4;
    faces[3].f92 = 8;
    faces[4].f90 = 2;
    faces[4].f92 = 2;
    faces[5].f90 = 1;
    faces[5].f92 = 7;
    faces[6].f90 = 2;
    faces[6].f92 = 2;
    faces[7].f90 = 1;
    faces[7].f92 = 4;
    for (i = 0; i <= 7; i++) {
        p = (char *)v7d6c8;
        faces[i].f94 = (faces[i].f90 + 0xc + faces[i].f92) * 0x428;
        for (j = 0; j < 2; j++) {
            faces[i].img[12 + j] = p;
            p += 0x428;
        }
        for (j = 0; j < 2; j++) {
            faces[i].img[14 + j] = p;
            p += 0x428;
        }
        for (j = 0; j < 8; j++) {
            faces[i].img[16 + j] = p;
            p += 0x428;
        }
        for (j = 0; j < faces[i].f90; j++) {
            faces[i].img[24 + j] = p;
            p += 0x428;
        }
        for (j = 0; j < faces[i].f92; j++) {
            faces[i].img[28 + j] = p;
            p += 0x428;
        }
    }
}

/* ---- 0x32b14, 144 bytes (callers chat 0x1x after ESC, f-net paths): drains the BIOS key buffer,
 * calls kbd_off, saves the current int 9 vector (DOS 0x3509: ES:EBX into oldkbd_seg/oldkbd_off) and
 * installs the game's keyboard handler (asm at 0x40907, outside the C modules) with DOS 0x2509.
 * The undo is in chat (0x2509 with oldkbd_off/oldkbd_seg, then kbd_off). */
extern int ioldkbd_seg;
#pragma aux ioldkbd_seg "_oldkbd_seg"
void kbd_isr(void);

void kbd_on(void)
{
    union REGS r;
    struct SREGS s;

    while (kbhit())
        getch();
    kbd_off();
    r.x.eax = 0x3509;
    s.es = 0;
    s.ds = 0;
    int386x(0x21, &r, &r, &s);
    ioldkbd_seg = s.es;
    oldkbd_off = r.x.ebx;
    r.x.eax = 0x2509;
    r.x.edx = FP_OFF(kbd_isr);
    s.ds = FP_SEG(kbd_isr);
    s.es = 0;
    int386x(0x21, &r, &r, &s);
}

/* ---- 0x32ba4, 169 bytes (callers kbd_on, chat, 0x32c50): clears every key-state byte the int 9
 * handler sets (0x88cc..0x88ce2 group, 0x7b6d9..0x7b6ef odd bytes) so no key stays held across a
 * handler change. */
extern char v88cd9, v88ce2, v88cd7, v88cda, v88cd4, v88cdc;
extern char kv7b6df, kv7b6e1, kv7b6e3;
#pragma aux kv7b6df "_v7b6df"
#pragma aux kv7b6e1 "_v7b6e1"
#pragma aux kv7b6e3 "_v7b6e3"

void kbd_off(void)
{
    key_select = 0;
    key88ccd = 0;
    v88cd9 = 0;
    v88ce2 = 0;
    v88cd7 = 0;
    v88cda = 0;
    v88cd4 = 0;
    v88cdc = 0;
    key_up = 0;
    key_down = 0;
    key88cd1 = 0;
    v88ce1 = 0;
    v7b6d9 = 0;
    v7b6db = 0;
    v7b6dd = 0;
    kv7b6df = 0;
    kv7b6e1 = 0;
    kv7b6e3 = 0;
    v7b6e5 = 0;
    v7b6e7 = 0;
    v7b6e9 = 0;
    v7b6eb = 0;
    v7b6ed = 0;
    v7b6ef = 0;
}

/* ---- 0x32c50, 71 bytes (no direct caller found): gives int 9 back (DOS 0x2509 with
 * oldkbd_off/oldkbd_seg) and clears the key state; the same steps chat does inline. */
void kbd_restore(void)
{
    union REGS r;
    struct SREGS s;

    r.x.eax = 0x2509;
    r.x.edx = oldkbd_off;
    s.ds = ioldkbd_seg;
    s.es = 0;
    int386x(0x21, &r, &r, &s);
    kbd_off();
}

/* ---- 0x32c98, 41 bytes (no direct caller found): pcx_blit with p7d714 = p, v88b4a = n and page_draw = a
 * for the call, page_draw restored after; the same steps race_run does inline for the demo records.
 * Followed by fade_out's loop tail (0x32cc1-0x32cf7), which the compiler emits here because fade_out
 * is defined next (see there); check.sh differs only in the tail's two rel32 jumps back to 0x1c03a /
 * 0x1c017 (LAYOUT, iter 56c). */
void pic_show(char *p, short n, short a)
{
    short save;

    save = page_draw;
    p7d714 = p;
    v88b4a = n;
    page_draw = a;
    pcx_blit();
    page_draw = save;
}

/* ---- 0x1bf94 and 0x32cc1-0x32cf7: fade the palette `p` out to black (callers: frame_draw, main_menu,
 * fn_1c968 and eight more). The mirror of fade_in 0x32cf8: a 0x300-byte working copy (signed bytes), dac_count =
 * 0x100 entries from dac_first = 0, palptr = the copy; with v88caa and musicon set the music fades
 * too (ticks = ticks2 = 0, MUSIC_FadeVolume(0, 4000)); every step lowers each entry above 0 by
 * v88c46 (read as a word for the test, 0x1c033), clamped at 0, then dac_load uploads it; stops after a
 * step with no entry above 0. The k counter (0..2) is computed but unused, as in fade_in.
 * Layout (CONFIRMED by this rebuild): defined HERE, after pic_show and before its mirror fade_in, the
 * compiler emits the loop tail (the clamp store, k, i++, dac_load, the epilogue) here, at 0x32cc1
 * straight after pic_show's `ret`, and the entry block where the tail call in fade_out_screen jumps to it,
 * at 0x1bf94; that is the game's layout. Instruction-identical in both parts; the only differing
 * bytes are the six rel32 displacements between them, each 0x2c4 short = the size our build lacks
 * between 0x1c068 and 0x32cc1, so a LAYOUT match. Spellings that mattered: the copy loop is a
 * `while` on k (a `for` gets an alignment nop; `for` on i swaps the i/k registers), the clamp in the
 * else arm, k short. */
extern short w88c46;
#pragma aux w88c46 "_v88c46"
extern short v88caa;
int MUSIC_FadeVolume(int tovolume, int milliseconds);

void fade_out(char *p)
{
    signed char cur[0x300];
    short hi, lo;
    short k, i, same;

    lo = 0;
    hi = 0x2ff;
    dac_count = 0x100;
    dac_first = 0;
    k = 0;
    while (k <= 0x2ff) {
        cur[k] = p[k];
        k++;
    }
    palptr = (char *)cur;
    if (v88caa && musicon) {
        ticks = 0;
        ticks2 = 0;
        MUSIC_FadeVolume(0, 4000);
    }
    do {
        same = -1;
        k = 0;
        for (i = lo; i <= hi; i++) {
            if (cur[i] > 0) {
                same = 0;
                if (cur[i] - w88c46 >= 0)
                    cur[i] -= w88c46;
                else
                    cur[i] = 0;
            }
            if (++k > 2)
                k = 0;
        }
        dac_load();
    } while (!same);
}

/* ---- 0x32cf8, 218 bytes (callers race_run 0x307a9, main_menu 0x1e9d4, 0x1ec2e, ...): fades the
 * VGA palette in from black to `target`: a 0x300-byte working palette (signed bytes) starts at 0
 * and every step moves each entry by v88c46 towards target, clamped, then dac_load uploads it
 * (palptr = working copy, dac_count = 0x100 entries from dac_first = 0); stops after a step with no
 * change. The k counter (0..2, the RGB component) is computed but unused.
 * differs: the init loop's i/k registers swap (game i EDX - `movsx eax,dx` 0x32d20, k EBX - `inc ebx`
 * 0x32d23, `cmp bx,2` 0x32d28), ours i EBX and k EDX; the schedule and every other instruction are
 * identical, so all 13 rows are those two homes (the main loop's i is ESI in both). ~15 variants tried
 * (declaration order, k int, k++ placement, loop index); 6 more in .work/probe_fi: comma-operand order
 * `for (k = 0, i = 0; ...)` swaps only the emission order of the two `xor`s, never the homes (14);
 * `k = 0;` as its own statement before the loop (14); `k++` and `if (k > 2)` as two statements (14);
 * `int k` moves k to EAX with `cmp eax,2` - 12 rows but no match; `short i, k` declaration order (17);
 * `hi` assigned before `lo` (16). Coin flip (GOTCHAS "Which pseudo gets EAX is a coin flip"). */
extern char v88c46;

void fade_in(unsigned char *target)
{
    signed char cur[0x300];
    short lo, hi;
    short k, i, same;

    lo = 0;
    hi = 0x2ff;
    for (i = 0, k = 0; i <= 0x2ff; i++) {
        cur[i] = 0;
        if (++k > 2)
            k = 0;
    }
    dac_count = 0x100;
    dac_first = 0;
    palptr = (char *)cur;
    do {
        same = -1;
        for (i = lo; i <= hi; i++) {
            if (cur[i] < target[i]) {
                cur[i] += v88c46;
                if (cur[i] > target[i])
                    cur[i] = target[i];
                same = 0;
            } else if (cur[i] > target[i]) {
                cur[i] -= v88c46;
                if (cur[i] < target[i])
                    cur[i] = target[i];
                same = 0;
            }
        }
        dac_load();
    } while (!same);
}

/* ---- 0x32dd4, 115 bytes (no direct caller found): size of the DAT entry `name` + pathext (the
 * music extension, .KLM / .MID); datfind seeks to it as a side effect. */
long song_size(char *name)
{
    char buf[20];

    strcpy(buf, name);
    strcat(buf, pathext);
    datfind(buf);
    return datdir[datidx].size;
}

/* ---- 0x32e60, 1963 bytes (callers main_menu before each race / zone): fills the song table
 * songs7c530[] (14-byte names + pathext; load_level reads entry v88b88 - 1, two-player and net
 * entry v88c90). One-player: three songs a/b/c chosen by class + metal (v88bb6 + v7ecb4, 1..6)
 * rotate through slots 0..3 (w88bce 1 -> a, 2 -> b, 3 -> c; it starts at 1), slot 4 is OVERDRIV.
 * Two-player / net: fixed BANSHI, KARD, TURBO, BRICWALL, STARBRAI, FLIGHT.
 * `&"NAME"[0]` gives the game's byte-loop strcpy (a plain literal gets movsd) and keeps the names
 * in CONST at 0x61b34 (extern arrays matched the code but shifted every later CONST string). */
extern char songs7c530[][14];
#pragma aux songs7c530 "_arr7c530"

void songs_pick(void)
{
    char a[16], b[16], c[16];
    int i;

    w88bce = 1;
    if (!twoplayer && !netvoices) {
        switch (v88bb6 + v7ecb4) {
        case 1:
            strcpy(a, &"TURBO"[0]);
            strcat(a, pathext);
            strcpy(b, &"DANCE"[0]);
            strcat(b, pathext);
            strcpy(c, &"FLIGHT"[0]);
            strcat(c, pathext);
            break;
        case 2:
            strcpy(a, &"STARBRAI"[0]);
            strcat(a, pathext);
            strcpy(b, &"BASSATTK"[0]);
            strcat(b, pathext);
            strcpy(c, &"KARD"[0]);
            strcat(c, pathext);
            break;
        case 3:
            strcpy(a, &"BANSHI"[0]);
            strcat(a, pathext);
            strcpy(b, &"STARBRAI"[0]);
            strcat(b, pathext);
            strcpy(c, &"BRICWALL"[0]);
            strcat(c, pathext);
            break;
        case 4:
            strcpy(a, &"FLIGHT"[0]);
            strcat(a, pathext);
            strcpy(b, &"KARD"[0]);
            strcat(b, pathext);
            strcpy(c, &"PUMPER"[0]);
            strcat(c, pathext);
            break;
        case 5:
            strcpy(a, &"DANCE"[0]);
            strcat(a, pathext);
            strcpy(b, &"BASSATTK"[0]);
            strcat(b, pathext);
            strcpy(c, &"TURBO"[0]);
            strcat(c, pathext);
            break;
        case 6:
            strcpy(a, &"BANSHI"[0]);
            strcat(a, pathext);
            strcpy(b, &"BRICWALL"[0]);
            strcat(b, pathext);
            strcpy(c, &"STARBRAI"[0]);
            strcat(c, pathext);
            break;
        }
        for (i = 0; i < 4; i++) {
            switch (w88bce) {
            case 1:
                strcpy(songs7c530[i], a);
                w88bce = 2;
                break;
            case 2:
                strcpy(songs7c530[i], b);
                w88bce = 3;
                break;
            case 3:
                strcpy(songs7c530[i], c);
                w88bce = 1;
                break;
            }
        }
        strcpy(songs7c530[4], &"OVERDRIV"[0]);
        strcat(songs7c530[4], pathext);
    } else {
        strcpy(songs7c530[0], &"BANSHI"[0]);
        strcat(songs7c530[0], pathext);
        strcpy(songs7c530[1], &"KARD"[0]);
        strcat(songs7c530[1], pathext);
        strcpy(songs7c530[2], &"TURBO"[0]);
        strcat(songs7c530[2], pathext);
        strcpy(songs7c530[3], &"BRICWALL"[0]);
        strcat(songs7c530[3], pathext);
        strcpy(songs7c530[4], &"STARBRAI"[0]);
        strcat(songs7c530[4], pathext);
        strcpy(songs7c530[5], &"FLIGHT"[0]);
        strcat(songs7c530[5], pathext);
    }
}

/* ---- 0x3360c, 185 bytes (caller 0x35ca4 at 0x36530, start-up): with music on, allocates the race
 * song buffer v7eb00 (0xcb20 bytes) and loads the five menu songs MAINMENU, LEADRBRD, GAMEOVER,
 * SPACEY, ASHES (+ pathext) into malloc'd buffers menusongs[0..4] (0x7c648; menusong = [0]);
 * v7ebb8 sums the bytes. Returns 0 when a malloc fails, else 1.
 * The name offset goes through `n` (the datsize local, homed in EAX) to get the game's EAX/EDX
 * roles; names[i] and six other spellings swap them. */
extern int v7ebb8;
extern char *menusongs[];
#pragma aux menusongs "_menusong"

void makepath_x(char *dst, char *src);
#pragma aux makepath_x "makepath_"

int songs_load(void)
{
    char names[5][12] = { "MAINMENU", "LEADRBRD", "GAMEOVER", "SPACEY", "ASHES" };
    char path[20];
    short i;
    long n;

    v7ebb8 = 0;
    if (musicon) {
        v7ebb8 = 0xcb20;
        v7eb00 = (int)malloc(0xcb20);
        if (!v7eb00)
            return 0;
        for (i = 0; i <= 4; i++) {
            n = i * 12;
            makepath_x(path, (char *)names + n);
            datfind(path);
            n = datsize();
            v7ebb8 += n;
            menusongs[i] = malloc(n);
            if (!menusongs[i])
                return 0;
            fread(menusongs[i], n, 1, datfile);
        }
    }
    return 1;
}

/* ---- 0x336c8, 2832 bytes (caller 0x35ca4 at 0x363f3, start-up): allocates every fixed buffer:
 * the voice / engine / horn sample buffers (only for fx cards other than -1, 2 and 7), the six kart
 * sprite set names and sizes (kartsets[], 38X28 .. 18X13.INF), the level block v7d750 (0x4fa00 +
 * v7d648 * v7d644 * 4 + the set sizes), the car pool, palettes, picture buffers, sprite sets and
 * fonts. v7ebb8 sums the sizes; on a failure the request size goes to v7eb88, the message (the
 * game's own names: "Car_pool", "posmap", "sel_pal", ...) to the log when v88bea, and it returns 0.
 * Returns 1 after logging "tot malloc". */
extern char *A_voicedata;
#pragma aux A_voicedata "_voicedata"
extern char *A_enginedata;
#pragma aux A_enginedata "_enginedata"
extern char *A_sfx2data;
#pragma aux A_sfx2data "_sfx2data"
extern char *A_gigdata;
#pragma aux A_gigdata "_gigdata"
extern char *A_sfxdata;
#pragma aux A_sfxdata "_sfxdata"
extern char *A_v7d750;
#pragma aux A_v7d750 "_v7d750"
extern char *A_v7d708;
#pragma aux A_v7d708 "_v7d708"
extern char *A_map;
#pragma aux A_map "_map"
extern char *A_pal7d848;
#pragma aux A_pal7d848 "_pal7d848"
extern char *A_pal7d84c;
#pragma aux A_pal7d84c "_pal7d84c"
extern char *A_pal7d850;
#pragma aux A_pal7d850 "_pal7d850"
extern char *A_v7d858;
#pragma aux A_v7d858 "_v7d858"
extern char *A_p6a978;
#pragma aux A_p6a978 "_p6a978"
extern char *A_p6a97c;
#pragma aux A_p6a97c "_p6a97c"
extern char *A_p6a980;
#pragma aux A_p6a980 "_p6a980"
extern char *A_icons;
#pragma aux A_icons "_icons"
extern char *A_hogdata;
#pragma aux A_hogdata "_hogdata"
extern char *A_crackdata;
#pragma aux A_crackdata "_crackdata"
extern char *A_v7eb20;
#pragma aux A_v7eb20 "_v7eb20"
extern char *A_v7d820;
#pragma aux A_v7d820 "_v7d820"
extern char *A_v7d710;
#pragma aux A_v7d710 "_v7d710"
extern char *A_p7d718;
#pragma aux A_p7d718 "_p7d718"
extern char *A_v7d758;
#pragma aux A_v7d758 "_v7d758"
extern char *A_v7d85c;
#pragma aux A_v7d85c "_v7d85c"
extern char *A_wfont;
#pragma aux A_wfont "_wfont"
extern char *A_ofont;
#pragma aux A_ofont "_ofont"
extern char *A_ofont3;
#pragma aux A_ofont3 "_ofont3"
extern char *A_anidata;
#pragma aux A_anidata "_anidata"
extern char *A_ofont2;
#pragma aux A_ofont2 "_ofont2"
extern char *A_ajdata;
#pragma aux A_ajdata "_ajdata"
extern char *A_pufdata;
#pragma aux A_pufdata "_pufdata"
extern char *A_handledata;
#pragma aux A_handledata "_handledata"
extern char *A_lapdata;
#pragma aux A_lapdata "_lapdata"
extern char *A_sparkdata;
#pragma aux A_sparkdata "_sparkdata"
extern char *A_genef;
#pragma aux A_genef "_genef"
extern char *A_img7eb5c;
#pragma aux A_img7eb5c "_img7eb5c"
extern char *A_img7eb3c;
#pragma aux A_img7eb3c "_img7eb3c"
extern char *A_v7ed14;
#pragma aux A_v7ed14 "_v7ed14"
extern char *A_v7ec98;
#pragma aux A_v7ec98 "_v7ec98"
extern int v7eb88, v7d62c, v7eba4, v7eba0;

int mem_alloc(void)
{
    short i;
    int n;

    v7ebb8 = 0;
    if (fxcard != -1 && fxcard != 7 && fxcard != 2) {
        v7ebb8 += 0x29810;
        A_voicedata = malloc(0x29810);
        if (A_voicedata == NULL) {
            if (v88bea)
                logerr("VOC buffer failed");
            v7eb88 = 0x29810;
            return 0;
        }
        v7ebb8 += 0x942;
        A_enginedata = malloc(0x942);
        if (A_enginedata == NULL) {
            if (v88bea)
                logerr("Engine VOC failed");
            v7eb88 = 0x942;
            return 0;
        }
        v7ebb8 += 0xe3a;
        A_sfx2data = malloc(0xe3a);
        if (A_sfx2data == NULL) {
            if (v88bea)
                logerr("My VOC failed");
            v7eb88 = 0xe3a;
            return 0;
        }
        v7ebb8 += 0xa410;
        A_gigdata = malloc(0xa410);
        if (A_gigdata == NULL) {
            if (v88bea)
                logerr("Devil Voices Failed");
            v7eb88 = 0xa410;
            return 0;
        }
        A_sfxdata = malloc(0xfe5);
        if (A_sfxdata == NULL) {
            if (v88bea)
                logerr("Horn voice failed");
            v7eb88 = 0xfe5;
            return 0;
        }
    }
    kartsets[0].f00 = 0x519c;
    strcpy(kartsets[0].name, "38X28.INF");
    kartsets[1].f00 = 0x3c28;
    strcpy(kartsets[1].name, "28X28.INF");
    kartsets[2].f00 = 0x1e38;
    strcpy(kartsets[2].name, "14X28.INF");
    kartsets[3].f00 = 0x3a70;
    strcpy(kartsets[3].name, "32X24.INF");
    kartsets[4].f00 = 0xffc;
    strcpy(kartsets[4].name, "15X13.INF");
    v7d628 = 0;
    kartsets[5].f00 = 0x1324;
    strcpy(kartsets[5].name, "18X13.INF");
    for (i = 0; i < 6; i++)
        v7d628 += kartsets[i].f00;
    v7d640 = 0x3c00;
    v7d658 = 0x15e00;
    v7d604 = 0x36000;
    v7d62c = 0x2000;
    v7d600 = v7d648 * v7d644 * 4;
    n = 0x4fa00 + v7d600 + v7d628;
    v7ebb8 += n;
    A_v7d750 = malloc(n);
    if (A_v7d750 == NULL) {
        if (v88bea)
            logerr("Buffer failed");
        v7eb88 = n;
        return 0;
    }
    v7ebb8 += 0x1f2c0;
    A_v7d708 = malloc(0x1f2c0);
    if (A_v7d708 == NULL) {
        if (v88bea)
            logerr("Car_pool failed");
        v7eb88 = 0x1f2c0;
        return 0;
    }
    v7ebb8 += v7d62c;
    A_map = malloc(v7d62c);
    if (A_map == NULL) {
        if (v88bea)
            logerr("posmap failed");
        v7eb88 = v7d62c;
        return 0;
    }
    v7ebb8 += 0x300;
    A_pal7d848 = malloc(0x300);
    if (A_pal7d848 == NULL) {
        if (v88bea)
            logerr("sel_combat_pal failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_pal7d84c = malloc(0x300);
    if (A_pal7d84c == NULL) {
        if (v88bea)
            logerr("sel_pal failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_pal7d850 = malloc(0x300);
    if (A_pal7d850 == NULL) {
        if (v88bea)
            logerr("spare_pal failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_v7d858 = malloc(0x300);
    if (A_v7d858 == NULL) {
        if (v88bea)
            logerr("dum_pal failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0xfa00;
    A_p6a978 = malloc(0xfa00);
    if (A_p6a978 == NULL) {
        if (v88bea)
            logerr("Picbufs[0] Failed");
        v7eb88 = 0xfa00;
        return 0;
    }
    A_v7ed14 = A_p6a978;
    A_v7ec98 = A_p6a978;
    v7ebb8 += 0xabe0;
    A_p6a97c = malloc(0xabe0);
    if (A_p6a97c == NULL) {
        if (v88bea)
            logerr("picbufs[1] failed");
        v7eb88 = 0xabe0;
        return 0;
    }
    v7ebb8 += 0x13880;
    A_p6a980 = malloc(0x13880);
    if (A_p6a980 == NULL) {
        if (v88bea)
            logerr("Picbufs[2] Failed");
        v7eb88 = 0x13880;
        return 0;
    }
    v7ebb8 += 0x15c6;
    A_icons = malloc(0x15c6);
    if (A_icons == NULL) {
        if (v88bea)
            logerr("Icons sp Failed");
        v7eb88 = 0x15c6;
        return 0;
    }
    v7ebb8 += 0x1900;
    A_hogdata = malloc(0x1900);
    if (A_hogdata == NULL) {
        if (v88bea)
            logerr("Weapons sp Failed");
        v7eb88 = 0x1900;
        return 0;
    }
    v7ebb8 += 0x1458;
    A_crackdata = malloc(0x1458);
    if (A_crackdata == NULL) {
        if (v88bea)
            logerr("crack sp Failed");
        v7eb88 = 0x1458;
        return 0;
    }
    v7ebb8 += v7eba4;
    A_v7eb20 = malloc(v7eba4);
    if (A_v7eb20 == NULL) {
        if (v88bea)
            logerr("ingame sprites failed");
        v7eb88 = v7eba4;
        return 0;
    }
    v7ebb8 += v7eba0;
    A_v7d820 = malloc(v7eba0);
    if (A_v7d820 == NULL) {
        if (v88bea)
            logerr("effects Failed");
        v7eb88 = v7eba0;
        return 0;
    }
    v7ebb8 += 0x300;
    A_v7d710 = malloc(0x300);
    if (A_v7d710 == NULL) {
        if (v88bea)
            logerr("tmp_pal Failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_p7d718 = malloc(0x300);
    if (A_p7d718 == NULL) {
        if (v88bea)
            logerr("pp Failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_v7d758 = malloc(0x300);
    if (A_v7d758 == NULL) {
        if (v88bea)
            logerr("cpl Failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x300;
    A_v7d85c = malloc(0x300);
    if (A_v7d85c == NULL) {
        if (v88bea)
            logerr("menu_pal Failed");
        v7eb88 = 0x300;
        return 0;
    }
    v7ebb8 += 0x1ffe;
    A_wfont = malloc(0x1ffe);
    if (A_wfont == NULL) {
        if (v88bea)
            logerr("font1_ptr Failed");
        v7eb88 = 0x1ffe;
        return 0;
    }
    v7ebb8 += 0xf40;
    A_ofont = malloc(0xf40);
    if (A_ofont == NULL) {
        if (v88bea)
            logerr("font2_ptr Failed");
        v7eb88 = 0xf40;
        return 0;
    }
    v7ebb8 += 0x2240;
    A_ofont3 = malloc(0x2240);
    if (A_ofont3 == NULL) {
        if (v88bea)
            logerr("s_font Failed");
        v7eb88 = 0x2240;
        return 0;
    }
    v7ebb8 += 0xb6e0;
    A_anidata = malloc(0xb6e0);
    if (A_anidata == NULL) {
        if (v88bea)
            logerr("gigdat failed");
        v7eb88 = 0xb6e0;
        return 0;
    }
    v7ebb8 += 0xebc;
    A_ofont2 = malloc(0xebc);
    if (A_ofont2 == NULL) {
        if (v88bea)
            logerr("Logo failed");
        v7eb88 = 0xebc;
        return 0;
    }
    v7ebb8 += 0x10a0;
    A_ajdata = malloc(0x10a0);
    if (A_ajdata == NULL) {
        if (v88bea)
            logerr("aj Failed");
        v7eb88 = 0x10a0;
        return 0;
    }
    v7ebb8 += 0x10a0;
    A_pufdata = malloc(0x10a0);
    if (A_pufdata == NULL) {
        if (v88bea)
            logerr("puf Failed");
        v7eb88 = 0x10a0;
        return 0;
    }
    v7ebb8 += 0xe60;
    A_handledata = malloc(0xe60);
    if (A_handledata == NULL) {
        if (v88bea)
            logerr("handle Failed");
        v7eb88 = 0xe60;
        return 0;
    }
    v7ebb8 += 0x4c56;
    A_lapdata = malloc(0x4c56);
    if (A_lapdata == NULL) {
        if (v88bea)
            logerr("lap_sp Failed");
        v7eb88 = 0x4c56;
        return 0;
    }
    v7ebb8 += 0x168;
    A_sparkdata = malloc(0x168);
    if (A_sparkdata == NULL) {
        if (v88bea)
            logerr("orig_spark Failed");
        v7eb88 = 0x168;
        return 0;
    }
    v7ebb8 += 0x3964;
    A_genef = malloc(0x3964);
    if (A_genef == NULL) {
        if (v88bea)
            logerr("genef Failed");
        v7eb88 = 0x3964;
        return 0;
    }
    A_img7eb5c = malloc(0xf3c);
    v7ebb8 += 0xf3c;
    if (A_img7eb5c == NULL) {
        if (v88bea)
            logerr("Little maps failed");
        v7eb88 = 0xf3c;
        return 0;
    }
    A_img7eb3c = malloc(0xf3c);
    v7ebb8 += 0xf3c;
    if (A_img7eb3c == NULL) {
        if (v88bea)
            logerr("Little maps save SP failed");
        v7eb88 = 0xf3c;
        return 0;
    }
    if (v88bea)
        fprintf(errlog, "tot malloc=%d\n", v7ebb8);
    return 1;
}

/* ---- 0x341d8, 48 bytes (callers sound setup 0x1127, 0x1146, ...; T2.C): drains the key buffer,
 * prints "Press any key when ready.." and waits for a key. */
void waitkey(void)
{
    while (kbhit())
        getch();
    printf("    \n");
    printf("Press any key when ready..\n");
    getch();
}

/* ---- 0x34208, 408 bytes (no direct caller found): draws every player with f00 == 1 straight ahead
 * of the camera (no turn, no side offset: spr_x centred on 0xa0) with the 4th face frame
 * (img[4]) at the scale of its distance |view_y - y|; a player more than 1000 away gets f00 = 2.
 * A cut-down players_project; LIKELY the start-line view of the karts in front.
 * BEHAV iter 57: `v7d878 = (short)(spr_w - 1 + spr_y - 0xc7) * 4` re-read spr_w *after* it was
 * shortened by that same expression, so the clip offset came out 0; the game computes it once
 * (0x34332-0x34340 `mov ax,[spr_w]; dec eax; add ax,[spr_y]; sub eax,0xc7`), uses that EAX for both
 * `sub [spr_w],ax` and `cwde; lea eax,[eax*4]; mov [v7d878],eax` (0x3435f), so v7d878 = the rows cut
 * off the bottom * 4. Now the local `n`, the house spelling (players_project 0x255d4, src/ww.c:11353).
 * Same-shape (iter 57): the `+0x68` only the game has is the promoted loop pointer - 0x3425b
 * `lea ecx,[eax+0x68]` before `jmp 0x3437f` and 0x3437c `add ecx,0x68` (struct player is 0x68
 * bytes); we do the same on the global (`add [v7d800],0x68`), the ECX promotion below.
 * differs: the game keeps v7d800 in ECX (stored back each iteration, reloaded after the idiv and
 * the call), the same promotion players_project misses (QUESTIONS 2); 18 blocks follow from it.
 * Iter 36 (33 rows, 416 vs 408 bytes): `v7d800 = v7d800 + 1` in the for step (44 -> 33); not a
 * cause of the missing promotion (all diagnostic, none promotes): v88b72 replaced by a local
 * counter, the `f00 = 2` store removed, the spr_draw_scaled call removed, a fresh `_v7d800` alias used
 * only here, init order `v88b72 = 0, v7d800 = players`, `< 8`, `&players[0]`. Unlike minimap
 * (MATCH by computing in the global), the global here is already the working variable. */
void players_ahead(void)
{
    short n, h, k, c;      /* this order and `4 * n`: iter 74 permute.py, 27 -> 26 rows */
    int d;

    for (v7d800 = players, v88b72 = 0; v88b72 <= 7; v7d800 = v7d800 + 1, v88b72++) {
        if (v7d800->f00 != 1)
            continue;
        d = abs(view_y - v7d800->y);
        c = 0xa0;
        if (d > 1000) {
            v7d800->f00 = 2;
            continue;
        }
        k = (d - 0x7c) / 0x46;
        if (k < 0)
            k = 0;
        if (k > 9)
            k = 9;
        spr_y = tb6bd00[d];
        p7d838 = tab6e470[k];
        sprptr = faces[v88b72].img[4];
        p7d764 = p7d838;
        h = *p7d764++;
        spr_w = *p7d764;
        p7d838 += 2;
        spr_h = h;
        spr_y -= spr_w;
        if (spr_y > 0xc7)
            continue;
        c -= spr_h >> 1;
        v7d878 = 0;
        if (spr_w - 1 + spr_y > 0xc7) {
            n = spr_w - 1 + spr_y - 0xc7;
            spr_w -= n;
            if (spr_w <= 0)
                continue;
            v7d878 = 4 * n;   /* 0x3435f: the value computed before spr_w was shortened */
        }
        spr_x = c;
        spr_draw_scaled();
    }
}

/* ---- 0x343a0, 184 bytes (caller 0x348f4 at 0x34c08): draws the p6a980 picture at the scale of
 * distance d (row tb6bd00[d], frame size tab6e470[(d - 0x7c) / 0x46, 0..9]), lifted by `lift` and
 * centred on x.
 * differs (32 rows, .work/probe_pa 13 spellings, two compiles, control 32): three homes and one
 * statement. The game homes lift in ECX, x in ESI and spills d to its own slot (`mov [esp],eax`
 * 0x343a9, frame 4, two pushes) and reloads it for tb6bd00[d] (0x343cf); ours keeps d in ECX,
 * lift in ESI, x in EDI (three pushes, no slot) - the documented home coin flip. The row IS a
 * local in the game: `mov ax,[eax*2+0x6bd00]; mov ebx,eax` (0x343d2) then 32-bit `sub ebx,eax`,
 * `sub ebx,ecx` and one spr_y store (0x34439), plus a dead `mov bx,[spr_y]` after the call; an
 * `int y` local reproduces all of that but moves k from DX to AX (`cmp ax,9` vs the game's
 * `cmp dx,9`) and drops the dead reload - 33 rows; `short y` gives 16-bit arithmetic (`sub dx,
 * word [spr_w]`) 35 rows; a local copy of d takes the slot but turns the division into
 * `idiv dword [esp]` 31 rows of a worse shape. Also tried: int/short h, a w temp, a row pointer,
 * y declared first, `y -= spr_w; y -= lift;`, the two pointer statements in the other order (32,
 * but 180 bytes vs the game's 184). Nothing kept.
 * BEHAV iter 57: no behaviour bug, three same-shape items. `+0x2` (ours) vs `#0x2` (the game) is one
 * pointer step over the frame header: we `lea edx,[eax+2]` 0x343f8 and read `[eax]`/`[edx]`, the game
 * `mov dx,[eax]` 0x34410 then `add eax,2` 0x34413 and `mov ax,[eax]` 0x34425 - h and w, same two
 * shorts. The extra spr_w / spr_y reads are our `spr_y = spr_y - spr_w - lift`: the game holds the
 * row in EBX and w in EAX and stores spr_y once (0x34439), we store tb6bd00[d] early and re-read it;
 * no call sits between the two stores, so no other code sees the intermediate value. */
void pic_at(int d, int lift, short x)
{
    short k, h;

    k = (d - 0x7c) / 0x46;
    if (k < 0)
        k = 0;
    if (k > 9)
        k = 9;
    spr_y = tb6bd00[d];
    p7d838 = tab6e470[k];
    sprptr = p6a980;
    v7d878 = 0;
    p7d764 = p7d838;
    p7d838 += 2;
    h = *p7d764++;
    spr_h = h;
    spr_w = *p7d764;
    spr_y = spr_y - spr_w - lift;
    spr_x = x - (h >> 1);
    spr_draw_scaled();
}

/* ---- 0x34458, 860 bytes (caller 0x35ca4 at 0x3677f, start-up): the release check. 42.GAM in the
 * DAT means registered and upgraded (v7eca0 3), 20.GAM registered (2), neither shareware (1, and
 * v7e574 1). Shows CHECK.PCX (palette from its last 0x300 bytes into v7d858) with the release
 * lines, the copyright and PRESS FIRE; unless v7ecbc, fades in and waits up to 2000 ticks for fire
 * (or the key_select key) or Esc.
 * differs: only the stack slots of n and y swap (game n esp+0x190 and y esp+0x194 - fndiff's game
 * column at 0x344f5 is `mov dword [esp+0x190],2` and at 0x345f0 `mov dword [esp+0x194],0x54`; ours
 * the reverse); declaration order, placement and int/short mixes do not move them. Documented dead end: Watcom's
 * scalar slot permutation (GOTCHAS "Watcom does not allocate scalar stack slots in declaration
 * order", 43 declaration orders probed for enter_name). Iter 30: y first assigned at the top or
 * before the v7e574 test (8 rows), int n, y (34 rows). Iter 54: `volatile short n, y` - 18 rows, the
 * two slots unchanged (0x190/0x194): volatile forces memory but not the slot order. */
extern int v7ecbc;

void release_check(void)
{
    char lines[10][40];
    short n, y;
    short i;
    char *pal;

    strcpy(lines[1], "WACKY WHEELS VERSION 1.1");
    v88bd6 = datfind("42.GAM");
    if (!v88bd6) {
        v88bd6 = datfind("20.GAM");
        if (!v88bd6) {
            v7e574 = 1;
            v7eca0 = 1;
        } else {
            v7e574 = 0;
            v7eca0 = 2;
        }
    } else {
        v7e574 = 0;
        v7eca0 = 3;
    }
    if (v7e574) {
        n = 2;
        strcpy(lines[2], "SHAREWARE RELEASE.");
    } else {
        n = 4;
        strcpy(lines[2], "THIS IS REGISTERED");
        if (v7eca0 == 3)
            strcat(lines[2], " AND UPGRADED");
        strcat(lines[2], ".");
        strcpy(lines[3], "IF YOU DID NOT PURCHASE THIS COPY");
        strcpy(lines[4], "THEN PLEASE SELECT ORDERING.");
    }
    pcx_load("CHECK.PCX", 0);
    pal = v7d858;
    datfind("CHECK.PCX");
    seekdat(telldat() + datsize() - 0x300);
    y = 0x54;
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    v88b4a = 0;
    page_draw = 0x3e80;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0x3e80;
    for (i = 1; i <= n; i = 1 + i) {    /* this spelling gives the game's slot order (permute.py, iter 56) */
        print_at(center_x(strlen(lines[i]) * 8), y, lines[i], 0);
        y += 10;
    }
    strcpy(lines[1], "COPYRIGHT BEAVIS-SOFT 1994");
    print_at(center_x(strlen(lines[1]) * 8), 0xa0, lines[1], 0);
    print_at(center_x(strlen("PRESS FIRE") * 8), 0xb8, "PRESS FIRE", 0);
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    if (!v7ecbc) {
        fade_in((unsigned char *)v7d858);
        wait_release(0);
        ticks = 0;
        do {
            v7d8a4 = v7d8ac;
            ctl_setup(0);
            buttons_poll();
            if (key_select)
                joy88b40 = 1;
        } while (!joy88b40 && key88cd0 != 1 && ticks < 2000);
        wait_release(0);
    }
}

/* ---- 0x347b4, 320 bytes (caller 0x348f4 at 0x34b63): draws the 60 32x32 tiles of v7d75c
 * (0x400 bytes each) as a 10 x 6 grid with grab32, then fills the 128 x 128 tile-pointer map at
 * tilemap (through v7d688) with them repeated: row r of the map uses tile row r % 6, column c tile
 * c % 10. LIKELY the background of a menu screen. */
void grab32(void);

void tiles_setup(void)
{
    int i, j, k, row;
    char *p, *base;

    spr_h = 0x20;
    spr_w = 0x20;
    spr_y = 0;
    p = v7d75c;
    for (j = 0; j <= 5; j++) {
        spr_x = 0;
        for (i = 0; i <= 9; i++) {
            sprptr = p;
            grab32();
            p += 0x400;
            spr_x += 0x20;
        }
        spr_y += 0x20;
    }
    row = 0;
    v7d688 = (int *)tilemap;
    for (j = 0; j <= 0x7f; j++) {
        p = v7d75c + row * 0x2800;
        base = p;
        k = 0;
        for (i = 0; i <= 0x7f; i++) {
            *v7d688++ = (int)p;
            p += 0x400;
            if (++k > 9) {
                p = base;
                k = 0;
            }
        }
        if (++row > 5)
            row = 0;
    }
}

/* ---- 0x348f4, 2534 bytes (caller 0x35ca4 at 0x36784, start-up): the intro. Unless v7ecbc (skip):
 * the APOGEE song (+ pathext) into v7eb00; TABLES.DAT as a picture (up to 3000 ticks, fire / Esc
 * after 100); then APOG1.PCX as the camera flies along the demo track (curpos from 0x5a0, the view
 * growing from a 1-line strip, spr_y 0x81 / spr_w 1, by 6 a frame) while the p6a978 logo drops in
 * with pic_at once the view is full (voice 0xc); the APOGEE song plays once (to its end with music,
 * else 0x16 logo steps). Then the menu song, BEAVIS.PCX (0x110 ticks), and WINTRO.PCX with the
 * eight karts driving past one by one (players_ahead, voice 8 as each starts) until the last is
 * gone, 0x3c ticks and a key. Ends with the WINTRO palette copied to v7d858 and CHECK.PCX loaded
 * (palette into v7d85c). v7d998 is 1 during the fly-over and restored after.
 * differs (15 rows, iter 36): the fread buffer is loaded before its size in the game (0x3499a);
 * music88bcc = 0 sinks below the inlined music_start loads (0x34c5e, the zero-argument dead end); the
 * game hoists v7ea70 into EAX above the wait loop (0x3515d) where ours reads it in the loop; and the
 * `k + 1 == j` compare has its operands swapped (0x351ae `cmp esi,ecx`). The page_draw save went with
 * `void pcx_blit(void)` (21 -> 17) and `k + 1 == j` (was `j == k + 1`) fixed the homes (17 -> 15).
 * Tried iter 36: `t = v7ea70;` + `ticks - t` (37), `-v7ea70 + ticks` (15), `< 9` (38), `t = v7eb00`
 * for the fread buffer (31), `(int)` casts on k or j (15). Iter 48 (12 rows now): `j == k + 1` again
 * in a probe (14 - .work/probe_in) - 9.5b gives a comparison's source-LEFT operand both the cmp
 * destination AND the first register home (ECX here, EAX in car_laps), so flipping the C operands
 * swaps the two temps and leaves the emitted `cmp` alone; the other rows are the moved fread load,
 * the zero sink, that hoist, and the padding/epilogue rows at 0x3526a and 0x352d4. */
void vram_clear(void);
void rows_restore(void);
extern int v7aa98;

void intro(void)
{
    char buf[20];
    int oldmode, first, n, fly;
    short done;
    int lift, dist, y, w, snd, t;
    short i, k, j;
    char *pal;

    v88bd4 = 1;
    oldmode = v7d998;
    if (v7ecbc) {
        kartsets_load(0);
        goto out;
    }
    if (musicon) {
        makepath(buf, "APOGEE");
        datfind(buf);
        fread((void *)v7eb00, datdir[datidx].size, 1, datfile);
    }
    if (pcxfile_load("TABLES.DAT", 0)) {
        file_pal("TABLES.DAT", (unsigned char *)v7d858);
        palptr = pal7d850;
        getpalette();
        fade_out(pal7d850);
        v88b4a = 0;
        page_draw = 0x3e80;
        p7d714 = p6a978;
        pcx_blit();
        page_draw = 0x3e80;
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        fade_in((unsigned char *)v7d858);
        ticks = 0;
        do {
            if (ticks >= 100) {
                v7d8a4 = v7d8ac;
                ctl_setup(0);
                buttons_poll();
                if (key_select)
                    joy88b40 = 1;
                if (joy88b40 || key88cd0 == 1)
                    break;
            }
        } while (ticks < 3000);
    }
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    vram_clear();
    page_draw = 0xbb80;
    pcx_load("APOG1.PCX", 0);
    pal = v7d858;
    datfind("APOG1.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    v7d998 = 1;
    v88b4a = 0;
    i = page_draw;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = i;
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    tiles_setup();
    detail_lvl = 1;
    curpos = 0x5a0;
    view_x = 0xbcc;
    view_y = 0x834;
    page_draw = 0x3e80;
    ticks = 0;
    ticks2 = 0;
    first = 1;
    y = 0x81;
    w = 1;
    lift = 0x3c;
    dist = 0x462;
    fly = 0;
    n = 0;
    for (;;) {
        ticks = 0;
        spr_y = y;
        spr_w = w;
        floor_draw();
        rows_restore();
        if (fly)
            pic_at(dist, lift, 0x82);
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (!page_draw)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        if (first) {
            music_start(v7eb00, 0);
            dac_count = 0x100;
            dac_first = 0;
            first = 0;
            palptr = v7d858;
            dac_load();
        }
        while (ticks <= 6)
            ;
        if (musicon) {
            if (!MUSIC_SongPlaying() && n >= 8)
                break;
        } else if (n >= 0x16)
            break;
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
        if (joy88b40 || key88cd0 == 1)
            break;
        if (fly) {
            lift -= 4;
            if (lift < 0)
                lift = 0;
            dist -= 100;
            if (dist < 0x8c) {
                dist = 0x8c;
                n++;
            }
        }
        y -= 6;
        w += 6;
        if (y < 0) {
            w = 0x82;
            y = 0;
        }
        if (w > 0x82) {
            w = 0x82;
            y = 0;
        }
        if (y == 0 && w == 0x82) {
            path_pt_scale();
            v7d798 = probe_x;
            v7d79c = probe_y;
            curpos += 0x1e;
            if (curpos >= 0x780)
                curpos -= 0x780;
            path_pt_scale();
            v7d768 = v7d798 - probe_x;
            v7d76c = v7d79c - probe_y;
            view_x += v7d768;
            ticks2++;
            view_y += v7d76c;
            if (ticks2 > 3) {
                if (!fly) {
                    fly = 1;
                    voice_play(0xc);
                }
                ticks2 = 0;
            }
        } else
            view_y += 6;
    }
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    v7d998 = oldmode;
    music_start(menusong, 1);
    kartsets_load(0);
    pcx_load("BEAVIS.PCX", 0);
    pal = v7d858;
    datfind("BEAVIS.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    v88b4a = 0;
    page_draw = 0x3e80;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0x3e80;
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    fade_in((unsigned char *)v7d858);
    ticks = 0;
    do {
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
    } while (!joy88b40 && key88cd0 != 1 && ticks < 0x110);
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    pcx_load("WINTRO.PCX", 0);
    pal = v7d85c;
    datfind("WINTRO.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    v88b4a = 0;
    page_draw = 0x3e80;
    p7d714 = p6a978;
    pcx_blit();
    page_draw = 0x3e80;
    page_from = 0x3e80;
    page_to = 0x7d00;
    page_copy();
    page_show();
    if (key88ccd) {
        screenshot();
        if (key88ccd)
            while (key88ccd)
                ;
    }
    fade_in((unsigned char *)v7d85c);
    ticks = 0;
    while (ticks < 0x3c)
        ;
    page_from = 0x7d00;
    view_y = 0;
    for (v7d800 = players, i = 0; i <= 7; i++) {
        v7d800->f00 = 0;
        v7d800->x = view_x;
        v7d800++;
        v7d800[-1].y = view_y + 0x3c;
    }
    players[0].f00 = 1;
    k = 0;
    ticks = 0;
    ticks2 = 0;
    voices_reset();
    voice_play(8);
    snd = 8;
    for (;;) {
        v7ea70 = ticks;
        page_to = page_draw;
        page_copy();
        players_ahead();
        page_show();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (!page_draw)
            page_draw = 0x3e80;
        else
            page_draw = 0;
        while (ticks - v7ea70 <= 8)
            ;
        for (v7d800 = players, j = 0; j <= 7; j++, v7d800++) {
            done = v7d800->f00 == 2;
            if (v7d800->f00)
                v7d800->y += 0x1c;
            if (!v7d800->f00 && k + 1 == j && abs(v7d800->y - players[k].y) >= 400) {
                if (sfxon) {
                    if (FX_SoundActive(v7aa98))
                        FX_StopSound(v7aa98);
                    voice_play(snd);
                }
                k++;
                v7d800->f00 = 1;
            }
        }
        if (done)
            break;
        if (ticks < 0x3c)
            continue;
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        buttons_poll();
        if (key_select)
            joy88b40 = 1;
        if (joy88b40 || key88cd0 == 1)
            break;
    }
out:
    for (i = 0; i <= 0x2ff; i++)
        v7d858[i] = v7d85c[i];
    pcx_load("CHECK.PCX", 0);
    pal = v7d85c;
    datfind("CHECK.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
}

/* ---- 0x352dc, 69 bytes (missing from the start list; caller see names file): sets text mode 3
 * (int 10h AX 3), then asks for the display combination code (AX 0x1a00); returns 1 when the BIOS
 * answers AL 0x1a, i.e. a VGA BIOS is present. */
int vga_check(void)
{
    union REGS m, r;

    m.h.ah = 0;
    m.h.al = 3;
    int386(0x10, &m, &m);
    r.w.ax = 0x1a00;
    int386(0x10, &r, &r);
    return r.h.al == 0x1a;
}

/* ---- 0x35324, 261 bytes (caller trial_book 0x2f01b): writes the time-trial book trial70730
 * (0xe10 bytes) to <savedir>WACKY<n>.TIM. */
extern char savedir[];                  /* 0x7c158: directory prefix of the save files */
#pragma aux savedir "_arr7c158"

void trial_write(int n)
{
    char name[0x80];
    char num[0x28];
    FILE *f;

    strcpy(name, savedir);
    itoa(n, num, 10);
    strcat(name, "WACKY");
    strcat(name, num);
    strcat(name, ".TIM");
    f = fopen(name, "wb");
    fwrite(trial70730, 0xe10, 1, f);
    fclose(f);
}

/* ---- 0x3542c, 261 bytes (caller 0x1d0d0, the high-score entry): writes the score rows from
 * tbl77758 + 0x450 (0x19e0 bytes, six rows of 0x450) to <savedir>WACKY<n>.HI. */
void scores_write(int n)
{
    char name[0x80];
    char num[0x28];
    FILE *f;

    strcpy(name, savedir);
    itoa(n, num, 10);
    strcat(name, "WACKY");
    strcat(name, num);
    strcat(name, ".HI");
    f = fopen(name, "wb");
    fwrite(tbl77758 + 0x450, 0x19e0, 1, f);
    fclose(f);
}

/* ---- 0x35534, 483 bytes (caller 0x35ca4 at 0x36845, start-up): reads WACKY.DTT (0x60 bytes: four
 * records {name padded with blanks to 16, value}) into dtt6e768. When the file is missing it first
 * writes the defaults SHAUN 16, ANDY 12, NUGGET 8, RUEBEN 6. This is the Duck Shoot hall of fame
 * (duckfame, shown and updated by duck_screen). */

void duckfame_load(void)
{
    char name[0x80];
    char names[4][10] = { "SHAUN", "ANDY", "NUGGET", "RUEBEN" };
    int vals[4];
    FILE *f;
    int i;

    vals[0] = 0x10;
    vals[1] = 0xc;
    vals[2] = 8;
    vals[3] = 6;
    strcpy(name, savedir);
    strcat(name, "WACKY.DTT");
    f = fopen(name, "rb");
    if (!f) {
        for (i = 0; i < 4; i++) {
            strcpy(duckfame[i].name, names[i]);
            while (strlen(duckfame[i].name) < 0x10)
                strcat(duckfame[i].name, " ");
            duckfame[i].score = vals[i];
        }
        f = fopen(name, "wb");
        fwrite(duckfame, 0x60, 1, f);
        fclose(f);
        f = fopen(name, "rb");
    }
    fread(duckfame, 0x60, 1, f);
    fclose(f);
}

/* ---- 0x35718, 148 bytes (caller main_menu 0x20585): writes the Duck Shoot hall of fame to
 * <savedir>WACKY.DTT. */
void duckfame_write(void)
{
    char name[0x80];
    FILE *f;

    strcpy(name, savedir);
    strcat(name, "WACKY.DTT");
    f = fopen(name, "wb");
    fwrite(duckfame, 0x60, 1, f);
    fclose(f);
}

/* ---- 0x357ac, 970 bytes (callers detail_menu 0x... and start-up, n = the save slot v7ee5a):
 * reads <savedir>WACKY<n>.TIM into trial70730 (0xe10) and WACKY<n>.HI into tbl77758 + 0x450
 * (0x19e0); a missing file is first created from the DAT's defaults WAC<n>.OTT / WAC<n>.HTT. */
void records_load(int n)
{
    char name[0x80];
    char dname[0x50];
    char num[0x28];
    FILE *f;

    strcpy(name, savedir);
    itoa(n, num, 10);
    strcat(name, "WACKY");
    strcat(name, num);
    strcat(name, ".TIM");
    f = fopen(name, "rb");
    if (!f) {
        itoa(n, num, 10);
        strcpy(dname, "WAC");
        strcat(dname, num);
        strcat(dname, ".OTT");
        datfind(dname);
        fread(trial70730, 0xe10, 1, datfile);
        f = fopen(name, "wb");
        fwrite(trial70730, 0xe10, 1, f);
        fclose(f);
        f = fopen(name, "rb");
    }
    fread(trial70730, 0xe10, 1, f);
    fclose(f);
    strcpy(name, savedir);
    itoa(n, num, 10);
    strcat(name, "WACKY");
    strcat(name, num);
    strcat(name, ".HI");
    f = fopen(name, "rb");
    if (!f) {
        itoa(n, num, 10);
        strcpy(dname, "WAC");
        strcat(dname, num);
        strcat(dname, ".HTT");
        datfind(dname);
        fread(tbl77758 + 0x450, 0x19e0, 1, datfile);
        f = fopen(name, "wb");
        fwrite(tbl77758 + 0x450, 0x19e0, 1, f);
        fclose(f);
        f = fopen(name, "rb");
    }
    fread(tbl77758 + 0x450, 0x19e0, 1, f);
    fclose(f);
}

/* ---- 0x35b78, 54 bytes (caller end_screen): puts the text cursor at column x, row y (1-based;
 * int 10h AH 2, page 0); without `show` parks it below the screen (row 0x19, column 0). */
void cursor_at(int x, int y, int show)
{
    union REGS in, out;

    y--;
    x--;
    if (!show) {
        y = 0x19;
        x = 0;
    }
    in.h.ah = 2;
    in.h.dl = x;
    in.h.dh = y;
    in.h.bh = 0;
    int386(0x10, &in, &out);
}

/* ---- 0x35bb0, 241 bytes (caller 0x35ca4 at 0x36a2f, on exit): shows the DOS end screen `name`
 * from the DAT (0xfa0 bytes = 80 x 25 text cells, read into p6a978 and put on screen by textscreen_show),
 * cursor to the last row, waits for a key. When the entry is missing: beep, "Cannot find DOS end
 * screen %s", key. */
void textscreen_show(void);
void cursor_at_x(int x, int y, int show);
#pragma aux cursor_at_x "cursor_at_"

void end_screen(char *name)
{
    int found;

    found = datfind(name);
    if (!found) {
        while (kbhit())
            getch();
        putchar(7);
        printf("Cannot find DOS end screen %s\n", name);
        fflush(stdout);
        getch();
        return;
    }
    p7d714 = p6a978;
    fread(p6a978, 0xfa0, 1, datfile);
    while (kbhit())
        getch();
    textscreen_show();
    cursor_at_x(0x4f, 0x18, 1);
    getch();
}

/* ---- 0x35ca4, 3498 bytes: main (called from the C start-up 0x4374a). Initialises the globals,
 * opens WACKY.DAT and reads WACKY.ING into cfg7ee1c, finds WACKY.CFG (in %APOGEECD% when set),
 * checks its size (0xada) and its 0xfa marker, parses the command line (cheats TURBO 1, JUMP 2,
 * HOG 4, ICE 8, FIRE 0x10 into the karts' fe0 flags; NOW skips the intro; /1 /2 /3 the frame
 * rate v7ed08 12 / 6 / 0; /DEBUG the error log), checks for VGA, allocates (mem_alloc), starts
 * sound and the three timer tasks, loads songs, sprites and tables (VEL.TAB, VEL2.TAB, VIEW,
 * SLP), sets mode 0x13 with the BEACH.PCX palette, installs the keyboard handler, runs the
 * release check, the intro and the main menu; on the way out END1.PCX for the shareware
 * release, shuts everything down and shows the DOS end screen (WWSW.BIN / WWREG.BIN).
 * differs: near miss, 22 blocks, all register / stack-slot choices (k in eax not edx, argv
 * pointer in ecx not esi, the two palette pointers swapped, the REGS temporaries of the inlined
 * setvmode calls in other slots, `bad` via eax); the CONST strings and the control flow agree. */
struct task *TS_ScheduleTask(void (*f)(struct task *), int rate, int prio, void *data);
void TS_Dispatch(void);
void TS_Terminate(struct task *t);
void MUSIC_Shutdown(void);
void FX_Shutdown(void);
void rowtab_init(void);
void modex_init(void);
extern struct task *t7ee00, *t7ee04;
#pragma aux t7ee00 "_v7ee00"
#pragma aux t7ee04 "_v7ee04"
extern struct task *comtask_t;
#pragma aux comtask_t "_comtask"
extern int v7ecdc, v7ec78, keytab, v7ec54, rowtab, v7d81c, v7d818, w775e6;
#pragma aux w775e6 "_v775e6"
extern short v88caa, v88c78, v88c1c, kbd_busy;
extern short w7d328[8];
#pragma aux w7d328 "_arr7d328"
extern int kfe0_684f0, kfe0_685f4;
#pragma aux kfe0_684f0 "_v684f0"
#pragma aux kfe0_685f4 "_v685f4"
extern short tab706a0[8][8];
#pragma aux tab706a0 "_arr706a0"
extern short vel7e8c4[200];
#pragma aux vel7e8c4 "_arr7e8c4"
extern short vel2_7e584[200];
#pragma aux vel2_7e584 "_arr7e584"
extern char slp6e858[];
#pragma aux slp6e858 "_arr6e858"
extern short w77430;
#pragma aux w77430 "_v77430"
extern char cfg77128[];
#pragma aux cfg77128 "_cfg77128w"

void setvmode_x(char mode);
#pragma aux setvmode_x "setvmode_"
short datopen_s(char *name);
#pragma aux datopen_s "datopen_"
short save_config_s(short keep);
#pragma aux save_config_s "save_config_"
short mem_alloc_s(void);
#pragma aux mem_alloc_s "mem_alloc_"
extern short w88c46;
#pragma aux w88c46 "_v88c46"

int main(int argc, char **argv)
{
    char *pal, *endpal;
    FILE *f;
    char *env;
    short v, j, i, k;
    int bad, iscd, size, pos, vga, n;
    union REGS r;

    v7ebd4 = 0x2580;
    v7ecac = 0;
    signal(7, (void (*)(int))1);
    signal(4, (void (*)(int))1);
    v7ed34 = 0x50;
    v7ed08 = 0xc;
    countplays = 0;
    v7ecbc = 0;
    v7ecdc = 0;
    v88ca0 = 0;
    musicpaused = 0;
    v7ec78 = 0;
    v7d8ac = (int)cfg7b6d8;
    v7d8b0 = (int)cfg7b6d8 + 0xc;
    keytab = (int)cfg7b6d8;
    v7d8a4 = (int)cfg7b6d8;
    netvoices = 0;
    v7d998 = 0;
    v7ea68 = 0;
    netgame = 0;
    net7ed78 = 0;
    net88c14 = 0;
    v7ec54 = 0;
    v88caa = 0;
    music88bcc = 0;
    w88bce = 0;
    if (datopen_s("WACKY.DAT") == -1) {
        setvmode(3);
        printf("\n");
        printf("Could not find WACKY.DAT \n");
        waitkey_x();
        exit(1);
    }
    datfind("WACKY.ING");
    fread(cfg7ee1c, 0x492, 1, datfile);
    v7eba4 = *(int *)(cfg7ee1c + 0x18);
    v7eba0 = *(int *)(cfg7ee1c + 0x14);
    strcpy(savedir, "");
    strcpy(cfgname, "");
    iscd = 0;
    env = getenv("APOGEECD");
    if (env) {
        strcpy(cfgname, env);
        size = strlen(cfgname);
        if (size == 0)
            strcpy(cfgname, "\\");
        else if (cfgname[size - 1] != '\\')
            strcat(cfgname, "\\");
        strcpy(savedir, cfgname);
        printf("APOGEECD enviroment label found\n");
        printf("Path=%s\n", cfgname);
        printf("WACKY.CFG will be written here.\n");
        iscd = 1;
    }
    strcat(cfgname, "WACKY.CFG");
    f = fopen(cfgname, "rb");
    if (!f) {
        setvmode(3);
        printf("Wacky Wheels Config File not found. Please run SETUP.EXE\n");
        printf("Press a key..\n");
        while (kbhit())
            getch();
        getch();
        exit(1);
    }
    pos = ftell(f);
    fseek(f, 0, 2);
    bad = ftell(f);
    fseek(f, pos, 0);
    bad = bad != 0xada;
    if (!bad) {
        fread(cfg7ee1c, 0x492, 1, f);
        fread(cfg7b6d8, 0x18, 1, f);
        fread(cfg77128, 0x630, 1, f);
        if (w77430 != 0xfa)
            bad = 1;
    }
    fclose(f);
    if (bad) {
        setvmode(3);
        printf("Incorrect Wacky Wheels Config File!\n");
        printf("Please run SETUP.EXE to correct this\n");
        printf("If this error persists your SETUP.EXE\n");
        printf("May not be the correct version.\n");
        printf("Your WACKY.CFG file may also be corrupted\n");
        printf("Delete this file and run SETUP.EXE\n");
        printf("Press a key..\n");
        while (kbhit())
            getch();
        getch();
        exit(1);
    }
    if (!save_config_s(0))
        exit(1);
    if (iscd)
        waitkey_x();
    v88c78 = 0;
    kfe0_684f0 = 0;
    kfe0_685f4 = 0;
    v88bea = 0;
    if (argc - 1 > 0)
    for (i = 1; i < argc; i++) {
        strupr(argv[i]);
        if (!strcmp(argv[i], "TURBO"))
            kfe0_684f0++;
        if (!strcmp(argv[i], "JUMP"))
            kfe0_684f0 += 2;
        if (!strcmp(argv[i], "HOG"))
            kfe0_684f0 = kfe0_684f0 + 4;
        if (!strcmp(argv[i], "ICE"))
            kfe0_684f0 += 8;
        if (!strcmp(argv[i], "FIRE"))
            kfe0_684f0 += 0x10;
        if (!strcmp(argv[i], "NOW"))
            v7ecbc = 1;
        if (!strcmp(argv[i], "/1"))
            v7ed08 = 0xc;
        if (!strcmp(argv[i], "/2"))
            v7ed08 = 6;
        if (!strcmp(argv[i], "/3"))
            v7ed08 = 0;
        if (!strcmp(argv[i], "/DEBUG")) {
            printf("Debug ON\n");
            v88bea = 1;
        }
    }
    kfe0_685f4 = kfe0_684f0;
    net7ed64 = kfe0_684f0;
    net7ed68 = kfe0_684f0;
    setvmode_x(3);
    r.w.ax = 0x1a00;
    int386(0x10, &r, &r);
    vga = r.h.al == 0x1a;
    if (!vga) {
        printf("WACKY WHEELS requires a VGA card you do not seem to have one\n");
        exit(1);
    }
    w88c46 = 4;
    w7d328[6] = 4;
    w7d328[7] = 5;
    w7d328[0] = 6;
    w7d328[1] = 7;
    w7d328[2] = 0;
    w7d328[3] = 1;
    w7d328[4] = 2;
    w7d328[5] = 3;
    v88c1c = 1;
    render_scales();
    if (v88bea || v7ec78)
        errlog = fopen("ERR.LOG", "wt");
    if (v7ec78)
        printf("Trace on\n");
    if (!mem_alloc_s()) {
nomem:
        setvmode(3);
        printf("Insufficient memory/Allocation failure. ");
        printf("Allocated %d bytes\n", v7ebb8 - v7eb88);
        printf("Needed a further %d bytes \n", v7eb88);
        printf("Please execute HELP_ME.EXE for possible solutions.\n");
        waitkey_x();
        exit(1);
    }
    if (!sound_init())
        exit(1);
    v7edbc = w775e6;
    if (v7edbc < 0 || v7edbc > 0x63)
        v7edbc = 0;
    ticks = 0;
    ticks2 = 0;
    racetime = 0;
    ticks3 = 0;
    t7ee00 = TS_ScheduleTask(tick_task, 0x88, 1, (void *)&ticks);
    t7ee04 = TS_ScheduleTask(tick_task, 10, 1, (void *)&racetime);
    comtask_t = TS_ScheduleTask(tick_task, 1, 1, (void *)&ticks3);
    TS_Dispatch();
    printf("Loading...\n");
    sounds_load();
    if (!songs_load())
        goto nomem;
    for (i = 0; i <= 0x2ff; i = i + 1)
        v7d858[i] = 0;
    kbd_off();
    ticks = 0;
    ticks2 = 0;
    sprites_load();
    k = 6;
    for (i = 0; i <= 7; i++) {
        v = k;
        for (j = 0; j <= 7; j++) {
            tab706a0[i][j] = v;
            if (++v > 7)
                v = 0;
        }
        if (--k <= -1)
            k = 7;
    }
    rowtab = (int)(size_t)lit7cbe8;
    rowtab_init();
    v7d81c = 0x462;
    v7d818 = 0x460;
    v88bd6 = datfind("VEL.TAB");
    if (!v88bd6 && v88bea)
        logerr("vel.tab");
    fread(vel7e8c4, 0x190, 1, datfile);
    datfind("VEL2.TAB");
    fread(vel2_7e584, 0x190, 1, datfile);
    for (n = 0; n <= 199; n++)
        tbl7e730[n] = vel7e8c4[n];
    v88bd6 = datfind("VIEW");
    if (!v88bd6 && v88bea)
        logerr("view");
    fread(tb6b7f8, 0x500, 1, datfile);
    v88bd6 = datfind("SLP");
    if (!v88bd6 && v88bea)
        logerr("slp");
    fread(slp6e858, 0x1e00, 1, datfile);
    v7d6d0 = v7d758 + 0x87;
    dac_count = 0x100;
    palptr = v7d858;
    dac_first = 0;
    setvmode_keep(0x13);
    dac_load();
    modex_init();
    pal = v7d758;
    datfind("BEACH.PCX");
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal, 0x300);
    pal_6bit_x((unsigned char *)pal);
    while (kbhit())
        getch();
    kbd_on();
    data_load();
    faces_reset();
    ytab_init();
    release_check();
    intro();
    v7d824 = 0xa7d00;
    v7d828 = 0xa7d50;
    v7d82c = 0xa7da0;
    v7d830 = 0xa7df0;
    v7d860 = 0xa7d50;
    v7d840 = 0xa7d50;
    page_draw = 0x3e80;
    kbd_busy = 0;
    key88cd5 = 0;
    key88cd6 = 0;
    key88cd3 = 0;
    key88cdd = 0;
    key88cd0 = 0;
    v88bb2 = 4;
    v88bb8 = 1;
    v88b86 = 1;
    detail_lvl = v7ee58;
    v88bb6 = 1;
    v7ea58 = v7ee5a;
    pnames_init();
    comopen = 0;
    duckfame_load();
    main_menu();
    if (v7e574) {
        palptr = pal7d850;
        getpalette();
        fade_out(pal7d850);
        pcx_load("END1.PCX", 2);
        endpal = pal7d850;
        datfind("END1.PCX");
        seekdat(telldat() + datsize() - 0x300);
        readdat(endpal, 0x300);
        pal_6bit_x((unsigned char *)endpal);
        v88b4a = 0;
        i = page_draw;
        p7d714 = p6a980;
        pcx_blit();
        page_draw = i;
        page_show();
        if (key88ccd) {
            screenshot();
            while (key88ccd != 0)
                ;
        }
        fade_in((unsigned char *)pal7d850);
        wait_release(0);
        wait_input(1);
    }
    v88caa = 1;
    palptr = pal7d850;
    getpalette();
    fade_out(pal7d850);
    TS_Terminate(t7ee00);
    TS_Terminate(t7ee04);
    TS_Terminate(comtask_t);
    if (musicon) {
        MUSIC_StopSong();
        MUSIC_Shutdown();
    }
    if (sfxon)
        FX_Shutdown();
    save_config(1);
    kbd_restore();
    setvmode_keep(3);
    setvmode(3);
    while (kbhit())
        getch();
    if (v88bea)
        fclose(errlog);
    if (v7e574)
        end_screen("WWSW.BIN");
    else
        end_screen("WWREG.BIN");
    if (datfile)
        fclose(datfile);
    exit(0);
}

