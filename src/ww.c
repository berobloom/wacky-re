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

/* 0x104-byte records; the base 0x68420 is a guess (lowest field seen so far: 0x68426) */
struct car {
    char f00[6];
    short face;
    char f08[0xc4 - 8];
    int sndhandle;
    char fc8[0xd0 - 0xc8];
    int fd0;
    char fd4[0x104 - 0xd4];
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

/* ---- HUD and modem dialogue */

extern short spr_h, spr_w, spr_x, spr_y;
extern short playing[2];
extern char *faceimgs[];
extern char *sprptr;
extern short p2offset;
void drawsprite(void);

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
            drawsprite();
        }
    }
}

extern short pal_a, pal_b, pal_c, pal_d;
extern short save_a, save_b, save_c;
extern int connected;
extern int ticks2;
extern char modeminit[];
extern char modemhangup[];
void setpalette(void);
void msgbox(char *s, int wait);
void fn_1c068(void);
void fn_1c0ac(void);

/* differs: the game calls com_getc here instead of inlining it (com_waitfor inlines it; -oe=20 is right for
   that); with com_getc not inlined (-oe=15) this function matches up to +443 */
int modem(int hangup)
{
    char msg[40];
    int ok;

    save_a = pal_a;
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
    if (hangup == 0)
        strcpy(msg, "INITIALIZING MODEM");
    else
        strcpy(msg, "DISCONNECTING");
    msgbox(msg, 0);
    connected = 0;
    sendfail = 0;
    while (com_getc() != -1)
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
            if (com_getc() == -1)
                break;
        }
    } else {
        sendstr(modeminit);
        ok = com_waitfor("OK");
    }
    pal_b = 48000;
    pal_a = pal_d;
    pal_c = pal_d;
    setpalette();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
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
void showlines(int n, int a, int b);

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
extern volatile char paused;
void drawsprite2(void);
void waitretrace(void);
void pausemenu(void);

/* differs: the game keeps the widened height in a stack slot ([esp]) and computes the bottom from it */
void drawsprite_clip(void)
{
    short h;
    int last;

    h = spr_w;
    last = h;
    clipy = 0;
    if (spr_y + last - 1 > 199) {
        clipy = (short)(h - 1 + spr_y - 199);
        spr_w = h - clipy;
    }
    if (spr_w > 0)
        drawsprite2();
    clipy = 0;
    spr_w = h;
}

void frame_wait(void)
{
    waitretrace();
    if (paused) {
        pausemenu();
        while (paused)
            ;
    }
}

/* differs: i and v swap registers, and the game shifts the whole register (shl edx, not shl bl) */
void pal_shift(unsigned char *p)
{
    short i;
    short v;

    for (i = 1; i <= 768; i++)
        {
        v = *p;
        *p++ = v << 2;
    }
}
