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
    char fa4[0xc0 - 0xa4];
    int fc0;                    /* 0xc0: v684e0 / v685e4 for cars 0 and 1 */
    int sndhandle;
    char fc8[0xd0 - 0xc8];
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

extern unsigned short pal_a, pal_b, pal_c, pal_d;
extern unsigned short save_a, save_b, save_c;
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
void drawsprite2(void);
void waitretrace(void);
void screenshot(void);

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
    if (shotkey) {
        screenshot();
        while (shotkey)
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
void getline(void);

/* differs: the game keeps &hdr in ebp and the palette pointer in [esp+0x50C]; ours swaps the pointer locals
   (declaration order has no effect) */
void screenshot(void)
{
    unsigned char palbuf[800];
    unsigned char line[324];
    struct pcxhdr hdr;
    char name[20];
    char digits[16];
    unsigned char *lineptr;
    unsigned char *pal;
    short n;
    short len, j;
    short i;
    short v;
    short x, run;
    short y;
    char *hp;
    FILE *f;

    pal = palbuf;
    lineptr = line;
    n = 1;
    sound(40);
    for (;;) {
        strcpy(name, "WACK");
        itoa(n, line, 10);
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
    for (i = 1; i <= 768; i++) {
        v = *pal;
        *pal++ = v << 2;
    }
    pal = palptr;
    hp = (char *)&hdr;
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
        getline();
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
                fputc(line[x], f);
                x++;
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

/* differs: the game loads the inlined loop argument (edx) before the stores; an explicit copy does not help */
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
        waitkey();
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

/* differs: edx/ebx constants swapped in the first unrolled loop (+27); statement order tried */
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

extern short v88c8a, sfxready, joyfound, v7ee4a, v7ee44;
extern int musicready, sfxsoft;
extern int musiccard, musicaddr, musicvol;
extern int fxcard, fxvoices, fxbits, fxvol;
extern char fxdevice[];
extern short keymap1, keymap2, keymap3;
int MUSIC_Init(int card, int address);
char *MUSIC_ErrorString(int err);
void MUSIC_SetVolume(int vol);
int FX_SetupCard(int card, void *device);
int FX_Init(int card, int voices, int channels, int bits, unsigned rate);
char *FX_ErrorString(int err);
void FX_SetVolume(int vol);
void joy_detect(void);

int sound_init(void)
{
    int status;

    v88c8a = 0;
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
            waitkey();
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
            waitkey();
            return 0;
        }
        status = FX_Init(fxcard, fxvoices, 1, fxbits, 11000);
        if (status != 0) {
            printf("%s\n", FX_ErrorString(status));
            waitkey();
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
    keymap1 = 1;
    keymap2 = 2;
    keymap3 = 3;
    input_clear1();
    joyfound = 0;
    v88c8a = 0;
    joy_detect();
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
    v88c8a = 1;
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

/* differs: first and the priority counter swap ebx/ecx in the first loop (+110); bytes otherwise equal */
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

/* differs: pri and the second loop's name pointer swap stack slots (0x168/0x16C; QUESTIONS 2); renaming
   pri and reordering declarations do not move them */
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
    for (i = 0; i < 27; i++) {
        strcpy(fname, names[i]);
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

/* differs: the game loads x before track (+8); otherwise equal */
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

struct kart {                   /* what curkart points at; layout still unknown */
    char f00[0x0A];
    short f0a;
    char f0c[0xB4 - 0x0C];
    short fb4;
};

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

extern short v88b0a, v88b0e, v88c6e, v88c72, v88c76, v88ca2, v88ca6, v88c6a, v88b7e;
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
    short f28[20];              /* 0x28-0x4F */
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
    v88c6e = v88b0a;
    v88c72 = v88b0e;
    v88c76 = myplayer;
    v7d898 = v7d860;
    curkart = kart7d5bc;
    v88b0a = v88ca2;
    v88b0e = v88ca6;
    curpos = players[1].pos;
    myplayer = v88c6a;
    v7d860 = v7d840;
    v88b7e = 1;
}

void view2_save(void)
{
    v88ca2 = v88b0a;
    v88ca6 = v88b0e;
    players[1].pos = curpos;
    v7d840 = v7d860;
}

void view2_out(void)
{
    v88b0a = v88c6e;
    v88b0e = v88c72;
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
    save = pal_a;
    spr_y = 4 + (players[n].place - 1) * 0x1C;
    pal_a = 0;
    print_at(0xF0, spr_y, buf, 0);
    sprptr = faces[players[n].face].img[0];
    spr_x = 0x10E;
    spr_y = 4 + (players[n].place - 1) * 0x1C;
    spr_h = 0x26;
    spr_w = 0x1C;
    drawsprite();
    pal_a = 16000;
    drawsprite();
    print_at(0xF0, spr_y, buf, 0);
    pal_a = save;
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

/* differs: the game inlines since2(cars[i].f82) and we call it (cf. modem and -oe); laptimes index in eax
   rather than edi (+203); the backward-lap score2 operands load in the other order */
void car_laps(void)
{
    int n;
    int i;
    int c;

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
                    c = cars[i].lap - 2;
                    laptimes[c] = c == 0 ? racetime : racetime - lastlap;
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
            if (curcell > cars[i].lastcell || since2(cars[i].f82) >= 10)
                cars[i].f96 = 0;
        }
        cars[i].lastcell = curcell;
        if (!cars[i].backward) {
            cars[i].score = (cars[i].lap << 10) + curcell;
            cars[i].score2 = curcell + cars[i].lap * ncells;
        } else {
            cars[i].score = ((cars[i].lap - 1) << 10) + curcell;
            cars[i].score2 = curcell + ncells * (cars[i].lap - 1);
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
            if (players[i].lap < nlaps + 1)
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
void blit372c7(void);

/* differs: pnames[v88c6a] is built in esi (imul esi; add $pnames) in the game, via eax here, in every
   strcpy that uses v88c6a; the v7eb80 ternary loads each index with movswl; 11 bytes short */
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
        drawsprite();
        if (v88ca0 == 1) {
            sprptr = img7eb3c;
            blit372c7();
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
            strcpy(buf, pnames[v7eb80 ? myplayer : v88c6a]);
            print_at(spr_x, spr_y, buf, 0);
            spr_x = 0x11;
            spr_y = 6;
            spr_h = 0x26;
            spr_w = 0x29;
            sprptr = img7d83c;
            drawsprite();
            if (netvoices) {
                spr_x = 0x1A;
                spr_y = 0xE;
                spr_h = 0x13;
                spr_w = 0xE;
                sprptr = img7eb60;
                drawsprite();
            }
        }
        if (!splitscreen && !netvoices) {
            if (!twoplayer) {
                spr_x = 0x44;
                spr_y = 6;
                spr_h = 0x41;
                spr_w = 0x13;
                sprptr = img7d814;
                drawsprite();
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
        drawsprite();
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
void setpalette(void);
void f27060(struct rec14 *r, int n);
void f1bf94(char *p);
short f375cd();                     /* asm; the game passes (EAX, EDX) and reads the result in EDX */
#pragma aux f375cd value [dx] parm [eax] [edx] modify []   /* 9.5b rejects value[edx]; [dx] gives the same bytes */
void f28480(void);
void f37e94(void);
void f287ec(void);
void f255d4(void);
void f25a78(void);
void f26a3c(void);
void f266a8(void);
void f2632c(void);
void f253d8(void);
void f3c3e9(void);
void f2a92c(void);
void f29e0c(void);
void f261fc(void);
void f37de0(void);
void f32cf8(char *p);

void frame_draw(void)
{
    if (v7d998 == 2)
        f27060(&rec7c208[v88c8e], 1);
    for (v88b72 = 0; v88b72 <= 1; v88b72++) {
        cars[v88b72].f96 = 0;
        cars[v88b72].f98 = 0;
        cars[v88b72].f94 = 0;
    }
    if (!v7d998) {
        palptr = pal7d850;
        getpalette();
        f1bf94(pal7d850);
    }
    v88b4a = 0;
    pal_a = 16000;
    p7d714 = p6a97c;
    f375cd();
    pal_a = 16000;
    if (v7d998 != 0 && v7d998 != 3) {
        pal_b = 16000;
        pal_c = 32000;
        setpalette();
    }
    f28480();
    if (v7eb80)
        f37e94();
    v88bf4 = 0;
    v88b78 = 0;
    p7d86c = arr79a28;
    f287ec();
    f255d4();
    f25a78();
    f26a3c();
    if (v88c38)
        f266a8();
    f2632c();
    if (v88b78)
        f253d8();
    if (twoplayer || splitscreen) {
        view2_in();
        f28480();
        if (v7eb82) {
            if (v88bde)
                f3c3e9();
            else
                f37e94();
        }
        v88bf4 = 0;
        v88b78 = 0;
        p7d86c = arr79a28;
        f287ec();
        f255d4();
        f25a78();
        f26a3c();
        if (v88c38)
            f266a8();
        f2632c();
        if (v88b78)
            f253d8();
        view2_out();
    }
    if (v88ca0)
        v88ca0 = 1;
    draw_status();
    f2a92c();
    f29e0c();
    f261fc();
    draw_faces();
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (!v7d998)
        f32cf8(p7d718);
    pal_a = 0;
    pal_b = 16000;
    pal_c = 0;
    setpalette();
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

/* ---- centered double-print (0x17888) */

void fn_17888(char *s, short y)
{
    int save;
    int x;

    save = pal_a;
    pal_a = 0;
    x = (0x140 - (short)((int)strlen(s) * 8)) / 2;
    if (y == 0)
        y = 0x60;
    print_at((short)x, y, s, 0);
    pal_a = 0x3e80;
    print_at((short)x, y, s, 0);
    pal_a = save;
}

/* ---- my DAT player record load (0x173C4) */

extern short v88b7e;
extern short v88c4e, v88c50, v88c52;
extern int v7d6c8, v7d868, v7d87c, v7d884, v7d888;
extern char arr7bba0[];               /* per-player 12-byte DAT file names */
extern char sp7b230b[], sp7b2d0b[], sp7b324b[], sp7b374b[];
extern char sp7b418b[], sp7b448b[], sp7b4d8b[], sp7b518b[];
extern char arr7418c[];
void fn_173c4(void);

void fn_173c4(void)
{
    short mp;
    int *q;
    int eax;                       /* byte cursor (name kept: matches game shape) */
    int i;
    int esi, edx;
    char *p;
    struct face *rec;

    mp = myplayer;
    q = (int *)(size_t)v7d868;
    v7d884 = 0x70660;
    v7d888 = 0x6e728;
    if (v88b7e == 0) {
        rec = &faces[mp];
        datfind(arr7bba0 + mp * 0xc);
        fread((void *)(size_t)v7d6c8, rec->f94, 1, datfile);
        v7d87c = v7d884;
    } else {
        v7d87c = 0x6e728;
        p = (char *)(size_t)v7d6c8 + 0x10a0;
        datfind(arr7bba0 + mp * 0xc);
        fread(p, 0x10a0, 1, datfile);
        rec = &faces[mp];
        eax = 0;
        do {
            eax += 4;
            *(int *)(arr7418c + eax) = *(int *)((char *)rec + eax + 0x2c);
            *(int *)(arr7418c + eax + 8) = *(int *)((char *)rec + eax + 0x34);
        } while (eax != 8);
        eax = 0;
        do {
            eax += 4;
            *(int *)((char *)rec + eax + 0x2c) = (int)p;
            p += 0x428;
        } while (eax != 8);
        eax = 0;
        do {
            eax += 4;
            *(int *)((char *)rec + eax + 0x34) = (int)p;
            p += 0x428;
        } while (eax != 8);
    }
    rec = &faces[mp];
    q = (int *)(size_t)v7d87c;
    q[0] = rec->img[1];
    q[2] = rec->img[2];
    q[4] = rec->img[3];
    q[6] = rec->img[5];
    q[8] = rec->img[6];
    q[10] = rec->img[7];
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
        *(int *)(sp7b324b + eax) = *(int *)((char *)&faces[mp] + eax + 0x44);
    } while (eax != 0x20);
    v88c4e = faces[mp].f90;
    i = 1;
    eax = 4;
    while (i <= v88c4e) {
        eax += 4;
        *(int *)(sp7b2d0b + eax) = *(int *)((char *)&faces[mp] + eax + 0x60);
        i++;
    }
    v88c52 = faces[mp].f92;
    esi = 0x10;
    edx = 0;
    do {
        edx += 4;
        *(int *)(sp7b374b + edx) = *(int *)((char *)&faces[mp] + esi);
        esi -= 4;
    } while (esi != -4);
    i = 1;
    esi = 4;
    edx = 0;
    while (i <= v88c52) {
        esi += 4;
        edx += 4;
        *(int *)(sp7b374b + edx) = *(int *)((char *)&faces[mp] + esi + 0x68);
        i++;
    }
    *(int *)(sp7b518b + v88b7e * 4) = faces[mp].img[12];   /* +0x30 */
    *(int *)(sp7b518b + 8 + v88b7e * 4) = faces[mp].img[13];
    *(int *)(sp7b518b + 0x10 + v88b7e * 4) = faces[mp].img[4];
    *(int *)(sp7b518b + 0x18 + v88b7e * 4) = faces[mp].img[14];
    *(int *)(sp7b518b + 0x20 + v88b7e * 4) = faces[mp].img[15];
    *(int *)(sp7b4d8b + v88b7e * 4) = faces[mp].img[3];
    *(int *)(sp7b4d8b + 8 + v88b7e * 4) = faces[mp].img[12];
    *(int *)(sp7b4d8b + 0x10 + v88b7e * 4) = faces[mp].img[13];
    *(int *)(sp7b4d8b + 0x18 + v88b7e * 4) = faces[mp].img[4];
    *(int *)(sp7b4d8b + 0x20 + v88b7e * 4) = faces[mp].img[14];
    *(int *)(sp7b4d8b + 0x28 + v88b7e * 4) = faces[mp].img[15];
    *(int *)(sp7b518b + 0x30 + v88b7e * 4) = faces[mp].img[5];
    v88c52 += 5;
    eax = 4;
    edx = 0;
    do {
        *(int *)(sp7b448b + edx + v88b7e * 4) = (int)faces[mp].img[eax];
        if (++eax > 7)
            eax = 0;
        edx += 8;
    } while (edx != 0x88);
    *(int *)(sp7b418b + v88b7e * 4) = faces[mp].img[9];    /* +0x24 */
    *(int *)(sp7b418b + 8 + v88b7e * 4) = faces[mp].img[8];
    *(int *)(sp7b418b + 0x10 + v88b7e * 4) = faces[mp].img[4];
    *(int *)(sp7b418b + 0x18 + v88b7e * 4) = faces[mp].img[10];
    *(int *)(sp7b418b + 0x20 + v88b7e * 4) = faces[mp].img[11];
    *(int *)(sp7b230b + v88b7e * 4) = faces[mp].img[8];
    *(int *)(sp7b230b + 8 + v88b7e * 4) = faces[mp].img[9];
    *(int *)(sp7b230b + 0x10 + v88b7e * 4) = faces[mp].img[8];
    *(int *)(sp7b230b + 0x18 + v88b7e * 4) = faces[mp].img[4];
    *(int *)(sp7b230b + 0x20 + v88b7e * 4) = faces[mp].img[10];
    *(int *)(sp7b230b + 0x28 + v88b7e * 4) = faces[mp].img[11];
    *(int *)(sp7b230b + 0x30 + v88b7e * 4) = faces[mp].img[10];
    *(int *)(sp7b230b + 0x38 + v88b7e * 4) = faces[mp].img[4];
    v88c50 = 8;
    if (v88b7e != 0) {
        eax = 0;
        do {
            *(int *)((char *)&faces[mp] + 0x30 + eax) = *(int *)(arr7418c + eax + 4);
            *(int *)((char *)&faces[mp] + 0x38 + eax) = *(int *)(arr7418c + eax + 0xc);
            eax += 4;
        } while (eax != 8);
    }
    v7d868 = (int)q;
    myplayer = mp;
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
        drawsprite();
    }
}

struct attr {                   /* SPRITE.ATR, 40 bytes */
    char f00[4];
    short w, h;
    char f08[0x0E - 8];
    short nframes;
    char f10[0x24 - 0x10];
    char *data;
};

extern short *p7d838, *p7d764;
extern int v7d878;
extern short *tab6e470[], *tab6e560[];
extern int v7d338[];
extern short v88c68;
void f37487(void);

/* differs: the game enters the loop with a jmp to the test at the bottom, we test at the top and duplicate
   the epilogue (+10 bytes); n and the w temporary swap stack slots */
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
            f37487();
        } else {
            if (!custom) {
                spr_h = 0x26;
                spr_w = 0x1C;
            } else {
                spr_h = ((struct attr *)sprattr)[id].w;
                spr_w = ((struct attr *)sprattr)[id].h;
            }
            drawsprite();
        }
        two = (twoplayer || splitscreen) ? 1 : 0;
        if (sel == -1 && v7d338[i]) {
            spr_h = 0x36;
            spr_w = 9;
            sprptr = lp7d804;
            spr_x -= 0xB;
            spr_y += 0x12;
            drawsprite();
            spr_h = 0x26;
            spr_w = 0x1C;
            spr_x += 0xB;
            spr_y -= 0x12;
        }
        if (sel == -1 && two && v88b7e == 1 && i == v88c68) {
            sprptr = gn7eae8;
            drawsprite();
        }
    }
}

/* ---- text */

extern short v88c2e, v88c10;

/* differs: stack slots only: x, hw, hh are permuted (QUESTIONS 2) */
void wtext(int x, int y, char *s, int scaled)
{
    int i, ok, c;
    int hw, hh;
    short sx, sy;

    i = 0;
    if (strlen(s) == 0)
        return;
    if (scaled) {
        spr_h = v88c2e;
        spr_w = v88c10;
        hw = spr_h >> 1;
        hh = spr_w >> 1;
    } else {
        spr_h = 0xF;
        spr_w = 0xD;
    }
    spr_y = y;
    spr_x = x;
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
        if (scaled) {
            sx = spr_x;
            sy = spr_y;
            spr_x += 7;
            spr_y += 6;
            spr_x -= hw;
            spr_y -= hh;
            f37487();
            spr_x = sx;
            spr_y = sy;
        } else {
            drawsprite();
        }
        spr_x += 0xB;
    }
}

extern char *sfont[];
extern char v88ce8, v88ce9;
void f373ee(void);
void f371e4(void);

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
        f373ee();
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
            v88ce9 = 0x2C;
            v88ce8 = 0x26;
        } else {
            spr_x = x;
            v88ce8 = 0x2C;
            v88ce9 = 0x2C;
            spr_y = y;
            spr_h = strlen(s) * 8;
            spr_w = 8;
        }
        f371e4();
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
            drawsprite();
        }
        spr_x += 8;
    }
}

/* ---- controls, timing */

extern short v88b5c, v88b5e, v88b52, v88b3c, v88b3e, v88b48;

void ctl_setup(short which)
{
    v88c8a = 0;
    if (joyfound)
        v88c8a = 1;
    if (which == 0) {
        if (!v7ee4a)
            v88c8a = 1;
        v88b5c = ((short *)cfg7ee1c)[14];
        v88b5e = ((short *)cfg7ee1c)[15];
        v88b52 = ((short *)cfg7ee1c)[16];
        v88b3c = ((short *)cfg7ee1c)[17];
        v88b3e = ((short *)cfg7ee1c)[18];
        v88b48 = ((short *)cfg7ee1c)[19];
        keymap1 = 1;
        keymap2 = 2;
        keymap3 = 3;
    } else {
        if (((short *)cfg77128)[2])
            v88c8a = 1;
        v88b5c = ((short *)cfg77128)[4];
        v88b5e = ((short *)cfg77128)[5];
        v88b52 = ((short *)cfg77128)[6];
        v88b3c = ((short *)cfg77128)[7];
        v88b3e = ((short *)cfg77128)[8];
        v88b48 = ((short *)cfg77128)[9];
        keymap1 = 4;
        keymap2 = 8;
        keymap3 = 12;
    }
    joy_detect();
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
    v88c8a = 0;
    joy_detect();
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
short net_key(int key, int flag, int x);

/* differs: stack slots only (QUESTIONS 2): the save slots permute (sx 0x1b8, sw 0x1bc, s34 0x1c4 ... vs our
   0x1b8 s34 ...) and the five 80-byte buffers too: game mine@0, blank@0x50, theirs@0xa0, buf@0xf0,
   ch@0x140; ours ch@0, buf@0x50, mine@0xa0, theirs@0xf0, blank@0x140 for declaration order
   mine,theirs,blank,buf,ch (not plain reverse). Statement order around strlen/center_x also scheduled
   differently (spr_y=10 store after strlen in the game). */
void chat(void)
{
    char mine[80], theirs[80], blank[80], buf[80], ch[80];
    union REGS r;
    struct SREGS s;
    int sx, sw, sy, s34, x1, c8, s20, key, n2, sh, y1, s1c, x2, c9, y2, other, y2m, y1m;
    int n1, t;

    save_a = pal_a;
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
    s34 = v7ed34;
    s20 = v7ed20;
    s1c = v7ed1c;
    sh = spr_h;
    spr_h = 320;
    sw = spr_w;
    spr_w = 200;
    c9 = v88ce9;
    v88ce9 = 0x2C;
    c8 = v88ce8;
    v88ce8 = 0x26;
    sx = spr_x;
    spr_x = 0;
    sy = spr_y;
    spr_y = 0;
    f371e4();
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
    x1 = 8;
    y1 = 0x49;
    x2 = 8;
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
        if (other == 13) {
            stext((short)x2, (short)y2m, theirs);
            strcpy(theirs, blank);
            n2 = 0;
            stext((short)x2, (short)y2, theirs);
        }
        if (key == 8 && n1 > 0) {
            stext((short)(x1 + n1 * 8), (short)y1, " ");
            n1--;
            mine[n1] = ' ';
            stext((short)(x1 + n1 * 8), (short)y1, " ");
        }
        if (other == 8 && n2 > 0) {
            stext((short)(x2 + n2 * 8), (short)y2, " ");
            n2--;
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
        v88ce9 = 0xFF;
        v88ce8 = 0xFF;
        spr_h = 8;
        spr_w = 8;
        spr_x = x1 + n1 * 8;
        spr_y = y1;
        f371e4();
        spr_x = x2 + n2 * 8;
        spr_y = y2;
        f371e4();
    }
    spr_h = sh;
    spr_w = sw;
    v88ce9 = c9;
    v88ce8 = c8;
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
    pal_b = 48000;
    pal_a = pal_d;
    pal_c = pal_d;
    setpalette();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
}

/* ---- net-synchronized menu (used by every options screen: pumps chat/keys with the peer while open) */

extern short v88bb0, v88b86, v88bb6, v88bb8, v88c80, v88c28, v88cf2;
extern short v88bee, v88bf0, v88bf2, v88c00;
extern char v88ccd, v88cd2, v88cdb, v88cd3, v88cd5, v88cd6, v88cdd, v88cce;
extern char key88cd2, key88cdb, v7b6d9, v7b6db;
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
void f377ae(void);
void fn_1c068(void);
void fn_1c0ac(void);

/* differs: stack slots all permuted (QUESTIONS 2; the game's menu(n,d,mode,c) has mode@0xc4, c@0xc8, n@0xcc,
   d@0xd8, and 23 scalar slots: ours folds two into registers, frame 0xd8 vs 0xe0); the game also has a dead
   inline strlen(mnames) at +0x1d3 (second one, result unused). Block structure, calls and the switch jump
   table all agree. */
short menu(short n, short d, short mode, short c)
{
    char cbuf[0x48], pbuf[0x3c];        /* game slots 0x00 and 0x48; sizes make the frame 0xe0 */
    unsigned short savepal;
    short bounce, tk, cursor, mow, show13, quit, isnet, sel, anim, cnt, state, four, i;
    int w11, x0, yb, bx1, sy, ybase, d4;

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
        savepal = pal_a;
        spr_y = v88bf2;
        pal_a = 0;
        spr_h = v88c00;
        sprptr += 0x6400;
        spr_w = v88bee;
        blit372c7();
        pal_a = savepal;
    }
    if (mode == 0) {
        v88b4a = 0;
        p7d714 = p6a978;
        savepal = pal_a;
        f375cd();
        pal_a = savepal;
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
    pal_c = 0x7d00;
    pal_b = pal_a;
    setpalette();
    spr_x = bx1;
    spr_h = 0x14;
    spr_y = sy;
    sprptr = sp6a518[sel];
    spr_w = 0x17;
    drawsprite();
    spr_x += w11;
    sprptr = sp6a528[sel];
    drawsprite();
    f37de0();
    if (key88ccd)
        screenshot();
    while (key88ccd)
        ;
    if (d)
        f32cf8(v7d85c);
    if (pal_a)
        pal_a = 0;
    else
        pal_a = 16000;
    if (quit)
        net_end(0);
    ybase = yb - 7;
    pal_b = 0x7d00;
    for (;;) {
        v7ea70 = ticks;
        if (net7ed78) {
            if (netgame == 1) {
                v7d8a4 = v7d8ac;
                ctl_setup(0);
                f377ae();
                if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
                    joy88b40 = 0;
                if (v88cce)
                    joy88b40 = 1;
                if (v88cd2)
                    key88cd3 = 1;
                if (v88cdb)
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
                key88cd6 = v88cef & 1;
                key88cd3 = v88cef & 2;
                joy88b40 = v88cef & 4;
                v7ecc0 = v88cef & 0x40;
            }
        }
        if (anim == 0)
            sy = ybase + (cursor - 1) * 12;
        pal_c = pal_a;
        setpalette();
        if (mode == 0)
            ents_show(bounce, 4, 1, mode, show13);
        spr_x = bx1;
        spr_h = 0x14;
        spr_y = sy;
        sprptr = sp6a518[sel];
        spr_w = 0x17;
        drawsprite();
        spr_x += w11;
        sprptr = sp6a528[sel];
        drawsprite();
        if (v88bb0 == 0xe) {
            if (cursor == 1)
                strcpy(pbuf, " PLAYER ONE ");
            else
                strcpy(pbuf, " PLAYER TWO ");
            msgbox(pbuf, 0x3c);
        }
        if (c && netgame == 0)
            msgbox("LEE & JACKSON LAWNMOWERS", 0x3c);
        if (net7ed78 && netgame == 2 && mode == 0)
            msgbox("REMOTE IS CONTROLLING MENU", 0x3a);
        if (v7ecac)
            msgbox("BOTH SIDES MUST USE SAME RATE", 0x3a);
        f37de0();
        if (key88ccd)
            screenshot();
        while (key88ccd)
            ;
        if (pal_a)
            pal_a = 0;
        else
            pal_a = 16000;
        while (ticks - v7ea70 < 8)
            ;
        tk++;
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
            f377ae();
            if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
                joy88b40 = 0;
            if (v88cce)
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
            if (joy88b40 || key88cd3 || v88cd2 || v88cdb)
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
        if (key88cd3 || v88cd2) {
            cursor++;
            anim = 1;
            cnt = 0;
            if (cursor > n)
                cursor = 1;
            continue;
        }
        if (key88cd6 || v88cdb) {
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

extern short v7ee58, v774de, v88b9e, v88c80, v88c20;
extern int v7ebd4, v7ec9c, v7eca0, v7edb0, v7ecb8;
extern void fn_357ac(int v);

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

void engines_menu(int a)
{
    register int edxv;
    int t, u, p;

    v88bb0 = 0xd;
    v88b9e = 0;
    p = a;
    if (twoplayer != 0)
        return;
    if (netvoices != 0)
        return;
    edxv = 0;
    u = 0;
    if (a == 3) {
        edxv = 1;
        p = 0;
    }
    if (p == 2) {
        u = 1;
        p = 0;
    }
    if (p)
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
    fn_357ac(edxv);
    if (p)
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
    v88c20 = v7ee58;
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

    switch (v88c20) {
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
    v88c7a = v88c7c >> 1;
    i = v7ea58s;
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

void fn_16f90(void)
{
    int k;
    int j, d;
    short bx;
    int ax, dx, absy;

    if (netvoices != 0) {
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
                players[k].f1a = *(short *)0x72356;
            }
            j = 0;
            do {
                j += 2;
                *(short *)(0x6aab6 + j + k * 0x68) = 0;
                *(short *)(0x6aabe + j + k * 0x68) = 0;
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
        if (twoplayer == 0) {
            j = 4;
            bx = order_a[0] + 0x50;
            for (d = 1; d <= 8; d++) {
                order_a[j] = bx;
                order_b[j] = order_b[0];
                bx -= 0x14;
                j--;
                if (j < 0)
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
                *(short *)(0x6aab6 + j + k * 0x68) = 0;
                *(short *)(0x6aabe + j + k * 0x68) = 0;
            } while (j != 4);
            players[k].x = order_a[k];
            players[k].y = order_b[k];
            players[k].f1a = *(short *)0x72356;
            ax = v88c06 - players[k].y;
            if (ax < 0)
                ax = -ax;
            players[k].f1c = 0;
            absy = ax;
            dx = v88c04 - players[k].x;
            if (dx < 0)
                dx = -dx;
            players[k].f10 = *(short *)0x72354 - absy;
            players[k].f5e = *(int *)0x72350 + absy * 4;
            if (players[k].x < v88c04)
                players[k].f00 = 3;
            if (players[k].x > v88c04)
                players[k].f00 = 4;
            players[k].f02 = 0;
            players[k].cell = 0;
            players[k].place = 0;
            players[k].lap = 0;
            players[k].f14 = 0;
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
        for (j = 7; j >= 0; j--) {
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

void fn_16d68(int a, int b, int face)   /* arg regs: EAX, EDX, EBX */
{
    short *p8;
    char *tbl = (char *)0x72350;
    int i;
    int idx;
    short f = (short)face;

    if (a <= 1) {
        p8 = (short *)(tbl + f * 8);
        WS(0x6ab14) = f;
        WS(0x6ab12) = p8[3];
        WS(0x6ab08) = p8[2];
        WI(0x6ab56) = *(int *)((size_t)(size_t)p8);
    } else {
        idx = (a - 1) * 0x68;
        p8 = (short *)(tbl + WS(0x6aaac + idx) * 8);
        WS(0x6aaaa + a * 0x68) = p8[3];
        WI(0x6aaee + a * 0x68) = WI(0x6aaee + idx);
        WS(0x6aaa0 + a * 0x68) = WS(0x6aaa0 + idx);
        WS(0x6aaac + a * 0x68) = WS(0x6aaac + idx);
        for (i = 1; i <= b; i++) {
            WS(0x6aaa6 + a * 0x68) = *(short *)WI(0x6aaee + a * 0x68);
            WI(0x6aaee + a * 0x68) += 2;
            WS(0x6aaa8 + a * 0x68) = *(short *)WI(0x6aaee + a * 0x68);
            WI(0x6aaee + a * 0x68) += 2;
            if (--WU(0x6aaa0 + a * 0x68) == 0) {
                if (++WU(0x6aaac + a * 0x68) > WU(0x88b7a))
                    WS(0x6aaac + a * 0x68) = 0;
                p8 = (short *)(tbl + WS(0x6aaac + idx) * 8);
                WS(0x6aaaa + idx) = p8[3];
                WI(0x6aaee + idx) = *(int *)((size_t)(size_t)p8);
                WS(0x6aaa0 + idx) = p8[2];
            }
        }
    }
    WI(0x7d7f8) = (int)(size_t)p8;
}

void fn_16f90(void);

void fn_16eac(void)
{
    short n, cnt;
    int i;

    n = nplayers;
    cnt = 0;
    for (i = 1; i <= n; i++)
        if (players[i].place == 2 && players[i].f56 == 4)
            cnt++;
    nplayers = n;
    if (cnt >= n) {
        n += 2;
        v7eca4++;
        nplayers = n;
        if (n > 0x12) {
            n = 0x12;
            nplayers = n;
        }
        fn_16f90();
        nplayers = nplayers;
    }
}

void srand(unsigned);
int rand(void);

void fn_16f24(void)
{
    int r;
    int m;
    int i;

    nplayers = v7ecb0;
    m = racetime;
    if (m < v88b7a)
        m = v88b7a;
    srand(m);
    for (;;) {
        do {
            r = rand();
        } while (r < 0 || r > v88b7a);
        for (i = 1; i <= nplayers; i++)
            fn_16d68(i, 0x104, r);
        break;
    }
}

/* differs: race-start setup (0x17928). Two compiler-side diffs (C shape tried extensively):
 *  1. +819 (0x17C5B): the game binds itoa's eax (movsx v88b88) before the strcpy edi/esi setup,
 *     we bind it after; all three compiler patch levels (95/95A/95B) and every source shape tried
 *     (statement order, nesting, probe isolation) give our order.
 *  2. ~0x18209/.18232: the game keeps `call readdat` (24 such sites game-wide); -oe in 9.5b always
 *     inlines readdat's body (fread(ptr,n,1,datfile): +11 bytes per site). Everything from 0x18209
 *     to the ret sits 22 bytes late in our object for that reason; semantics identical.
 * See findings/executables.md "Open" and GOTCHAS.md. */

extern short v88bd0, v88b88, v7eb82, v88bd6, v88b4a, v88c90;
extern int v7ed98, v7ec80, v7eb00;
extern char *v7eb54;
extern char *lapdata;
void fn_279e8(void);
void fn_2754c(void);
short fn_27920(int n);
void fn_2b5e8(int n);
void fn_32e60(void);
void fn_36f55(void);              /* asm: writes 0x6400 bytes from v7eb54 to A000:7D00 */

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
            WI(0x6e6e0 + edx) = eax;  eax += 0x27e;
            WI(0x6e6e4 + edx) = eax;  eax += 0x27e;
            WI(0x6e6e8 + edx) = eax;  eax += 0x27e;
            WI(0x6e6ec + edx) = eax;  eax += 0x27e;
        } while (edx != 0x20);
    }
    switch ((short)v88bb6 + v7ecb4 - 1) {
    case 0:
        strcpy((char *)0x80ace, "BRONZEM.SP");
        v7ed98 = 1;
        track = v88b88;
        if (v7ec80) {
            strcpy((char *)0x80ace, "SILVERM.SP");
            track = 7;
        }
        break;
    case 1:
        strcpy((char *)0x80ace, "SILVERM.SP");
        track = v88b88 + 5;
        break;
    case 2:
        strcpy((char *)0x80ace, "GOLDM.SP");
        track = v88b88 + 0xa;
        break;
    case 3:
        strcpy((char *)0x80ace, "BONUSB.SP");
        track = v88b88 + 0x15;
        break;
    case 4:
        strcpy((char *)0x80ace, "BONUSS.SP");
        track = v88b88 + 0x1a;
        break;
    case 5:
        strcpy((char *)0x80ace, "BONUSG.SP");
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
                    strcpy((char *)0x6aa68, "BONUS");
                } else {
                    strcat(msg, " FINAL ");
                    strcpy((char *)0x6aa68, "FINAL");
                }
            } else {
                itoa(v88b88, numbuf, 10);
                strcpy((char *)0x6aa68, "RACE ");
                strcat((char *)0x6aa68, numbuf);
                strcat(msg, " RACE ");
                strcat(msg, numbuf);
                strcat(msg, (char *)0x600a0);
            }
        }
    } else
        strcpy(msg, "LOADING ZONE");
    waitretrace();
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
            f27060((struct rec14 *)0x7eb6c, 2);
            palptr = pal7d850;
            getpalette();
            f1bf94(pal7d850);
            v88b4a = 0;
            p7d714 = p6a980;
            pal_a = 0;
            f375cd();
            pal_a = 0;
        }
        if (netvoices)
            fn_2b5e8(0);
        v88bd6 = datfind((char *)0x6a4b8);
        fread(p6a980, 0x6400, 1, datfile);
        if (v7d998 == 0 || v7d998 == 3) {
            v7eb54 = p6a980;
            fn_36f55();
        }
        pal_b = 0;
        pal_c = 0x3e80;
        setpalette();
        pal_a = 0;
        waitretrace();
        if (key88ccd) {
            screenshot();
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (v7d998 != 3 && v7ec80 == 0)
            f32cf8(p7d718);
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
    save_a = pal_a;
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 0xbb80;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
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
            pal_b = 0xbb80;
            pal_a = pal_d;
            pal_c = pal_d;
            setpalette();
            pal_b = save_b;
            pal_c = save_c;
            pal_a = save_a;
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
    pal_b = 0xbb80;
    pal_a = pal_d;
    pal_c = pal_d;
    setpalette();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
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
        strcpy((char *)0x7d7b8, (char *)0x7d770);
        strcat((char *)0x7d7b8, ".BMC");
        datfind((char *)0x7d7b8);
        readdat(&bmcn, 2);
        readdat((char *)0x7d9a0, bmcn * 3);
    }
music:
    if (musicon && v7d998 != 2) {
        if (twoplayer || netvoices)
            n = v88c90;
        else {
            n = v88b88 - 1;
        }
        datfind((char *)(0x7c530 + n * 0xe));
        n = WI(0x6508e + datidx * 0x16);
        if (cursong == v7eb00) {
            if (MUSIC_SongPlaying())
                MUSIC_StopSong();
        }
        fread((void *)(size_t)v7eb00, n, 1, datfile);
    }
}

/* ---- palette entry copier (0x182d0), used by the bonus menus: when a4 != 0
   copy 3*a3 bytes at dst + 3*a2 from palbase + a5 + 3*a2, else self-copy from
   dst + 3*a2 (the callers "flash" entries); callers pass a5 = pal7d84c base.
   CORRECTION: the earlier "dead code, no reference anywhere" note was WRONG,
   xref.py finds ~22 calls inside fn_1832c/fn_193a8; it is a real call in the
   game's bytes (-oe does not inline it).
   differs: the game has a dead register copy mov esi,ebx at 0x182d4 before
   imul esi,ebx,3 (+2 bytes, then everything shifts); every C form tried
   (t=a3 copy, comma, register, expression in both loops) gets folded by 9.5b,
   so it may be a precompiled-library artifact. */

void fn_182d0(char *dst, short a2, long a3, long a4, int a5)
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
   pin the rule; (b) the game CALLS datfind/telldat/datsize/seekdat/readdat (0x1418c-
   0x14260) while -oe inlines our wrappers (GOTCHAS); (c) loop counters use
   different scratch registers in the 0x1839c init loop. Strings, palette and
   net blocks reproduce the game's bytes. */

extern int v7c658, v7ecf0, v7ec80, v7ec9c, v7e574;
extern short w88b1e, w88b10, v88b88, v88c58;
extern char v88cec;
extern char *pal7d84c;
extern char p6aaae[];

extern int v684e0, v685e4;
extern char str7c854[], str7c890[], str7c8cc[];
void f26ec4(char *p);
void f278c4(int a);
void f37cf1(void);
void f2e4b8(int a, int b, int c);

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
    short s350, s354, s358, card_n, s360, s364, nav;
    char *gp;
    short i, up, down, ret;
    int eight;

    v7ecc4 = 0;
    v7ed20 = 0x440;
    v7ed1c = 0x2a8;
    eight = 8;
retry:
    v7ec80 = 0;
    s350 = 0;
    v684e0 = 0;
    v685e4 = 0;
    for (i = 0; i <= 7; i++)
        *(short *)(p6aaae + i * 0x68) = 0;
    for (i = 0; i <= 1; i++) {
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
    f1bf94(pal7d850);
    v88b88 = 1;
    pal_a = 0;
    pcx[0] = 0;
    if (v7ecb4 == 0)
        strcpy(card, "");
    else
        strcpy(card, "B");
    strcat(pcx, card);
    strcat(pcx, "SB1.PCX");
    datfind(pcx);
    seekdat(telldat() + datsize() - 0x300);
    readdat(pcx, 0x300);
    for (card_n = 1; card_n <= 4; card_n++) {
        f26ec4(pcx);
        itoa(card_n, num, 10);
        strcpy(fname, card);
        strcat(fname, "SB");
        strcat(fname, num);
        strcat(fname, ".PCX");
        f27060((struct rec14 *)fname, 2);
        v88b4a = 0;
        p7d714 = p6a980;
        pal_a = f375cd(p6a980, pal_a);
        pal_a = pal_a + 16000;
    }
    pal_a = (v88bb6 - 1) * 16000;
    frame_wait();
    gp = pal7d84c;
    for (i = 0; i <= 0x2ff; i++)
        pal[i] = gp[i];
    fn_182d0(pal, 0xe9, 1, 0, (int)gp);
    fn_182d0(pal, 0xec, 1, 0, (int)gp);
    fn_182d0(pal, 0xef, 1, 0, (int)gp);
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
    for (i = 5; i <= 9; i++)
        fn_182d0(pal, i, 1, 0, (int)gp);
    f32cf8(pal);
    f278c4(0);
    if (v88bb2 == 4 && v7ecf0 != -1) {
        v88b88 = (short)v7ecf0;
        palptr = pal;
        w88b1e = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 1, (int)gp);
        s354 = 1;
        w88b10 = 1;
        f37cf1();
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
    pal_a = (v88bb6 - 1) * 16000;
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
    f377ae();
    joy_clear_y();
    if (netgame == 0) {
        if (v88cce)
            joy88b40 = 1;
        if (key88cd3 || key88cd2)
            up = 1;
        if (key88cd6 || key88cdb)
            down = 1;
        if (up || down)
            nav = 1;
        goto merged;
    }
    if (netgame == 1) {
        if (v88cce)
            joy88b40 = 1;
        if (key88cd3 || key88cd2)
            up = 1;
        if (key88cd6 || key88cdb)
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
        v7ecc0 = 0;
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
        joy88b40 = v88cef & 4;
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
    s354++;
    if (s354 > 12) {
        w88b1e = v88bb6 * 3 + 0xe6;
        if (s364 == 0) {
            fn_182d0(pal, w88b1e, 1, 0, (int)gp);
            nav = 1;
            s364 = 1;
        } else {
            fn_182d0(pal, w88b1e, 1, 1, (int)gp);
            nav = 0;
        }
        s354 = 0;
        w88b10 = 1;
        palptr = pal;
        f37cf1();
    }
    if (s350 != 0) {
        joy88b40 = 1;
        nav = 0;
        up = 0;
        down = 0;
    }
    if (joy88b40) {
        voice_play(0x12);
        if (v88bb2 != 4)
            return;
        ret = 0;
        if (v7e574 && v88bb6 > 1) {
            voice_play(0x1a);
            ret = showlines(3, 1, 0, 1);
        }
        if (ret == 0)
            goto joyopen;
        s364 = 0;
        s354 = 0;
        w88b1e = v88bb6 * 3 + 0xe6;
        w88b10 = 1;
        fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 0, (int)gp);
        f37cf1();
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
    s354 = 0;
    w88b1e = v88bb6 * 3 + 0xe6;
    w88b10 = 1;
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 0, (int)gp);
    f37cf1();
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
        w88b1e = v88bb6 * 3 + 0xe6;
        fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
        f37cf1();
    }
    if (v88cec == 0)
        goto mainloop;
    for (i = 0; i <= 0x2ff; i++)
        p7d718[i] = pal[i];
    v88b9e = 1;
    return;

joyopen:
    palptr = pal;
    w88b1e = v88bb6 * 3 + 0xe6;
    fn_182d0(pal, v88bb6 * 3 + 0xe6, 1, 1, (int)gp);
    s364 = 0;
    w88b10 = 1;
    f37cf1();
    w88b1e = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 1, (int)gp);
    w88b10 = 1;
    f37cf1();
    f278c4(0);
joyloop:
    ticks = 0;
    v7ecf0 = v88b88;
    ticks = 0;
    v7d8a4 = v7d8ac;
    ticks2 = 0;
    ctl_setup(0);
    f377ae();
    joy_clear_y();
    if (v88cce)
        joy88b40 = 1;
    while (eight > ticks) {
        if (key88cd0 == 1)
            break;
    }
    while (key88cd0 == 1)
        ;
    palptr = pal;
    s364 = 0;
    w88b1e = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
    w88b10 = 1;
    f37cf1();
    goto mainloop;

/* the game jumps back to 0x19021 (joynav) from 0x1910f; C reaches it by fallthrough */
    s358++;
    if (s358 > 12) {
        w88b1e = v88b88 + 4;
        if (s360 == 0) {
            fn_182d0(pal, v88b88 + 4, 1, 1, (int)gp);
            s360 = 1;
        } else {
            fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
            s360 = 0;
        }
        s354 = 0;
        w88b10 = 1;
        palptr = pal;
        f37cf1();
    }
    up = 0;
    down = 0;
    nav = 0;
    if (key88cd3 || key88cd2)
        up = 1;
    if (key88cd6 || key88cdb)
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
    w88b1e = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
    w88b10 = 1;
    f37cf1();
    if (down) {
        v88b88--;
        if (v88b88 < 1)
            v88b88 = 5;
        w88b1e = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
        w88b10 = 1;
        f37cf1();
    }
    if (up) {
        v88b88++;
        if (v88b88 > 5)
            v88b88 = 1;
        w88b1e = v88b88 + 4;
        fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
        w88b10 = 1;
        f37cf1();
    }
    goto joyloop;

joyretry:
    voice_play(0x12);
    if (v88bb2 != 4)
        return;
    v88cec = 0;
    f2e4b8(v88bb6 + v7ecb4, v88b88, 0);
    if (v88cec == 0)
        return;
    palptr = pal;
    w88b1e = v88b88 + 4;
    fn_182d0(pal, v88b88 + 4, 1, 0, (int)gp);
    w88b10 = 1;
    f37cf1();
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
   differs (near-miss): (a) the game's build caches the global v7ebc0 (connected) in EBP
   across the menu's loops - 10 loads at loop entries and 5 stores at loop exits, ~90
   bytes, none of which our 9.5b build emits for this function (it does the same dance
   for wait_or_key itself, whose body this inlines); (b) frame 0x32c vs our 0x310 - 28
   bytes of slots the game uses somewhere (no [esp+0x30c..0x327] reference appears in
   its code, and an unused array we declare is dropped from the frame); (c) i lands in
   EDI and idx in a stack slot for us (SI/DI in the game), so the flash-toggle slot
   encodings at 0x19a19 differ; (d) the telldat/datsize/seekdat/readdat expression at
   0x194bc accumulates in EAX in the game and in EDX for us; (e) the f375cd call loads
   pal_a with a 16-bit partial load (mov 0x88b24,%dx) where our #pragma aux parm[edx]
   forces mov+movzwl. Blocks that do reproduce the game's bytes: the three message
   copies, the palette copy loops, the fn_182d0 flash loops, the input blocks for
   netgame 0/1/2, the net_key/v7ecc4 block, the two error/escape blocks and the two
   nav blocks. */

extern char *pal7d848;
extern short v88c96;
extern char key88cd2, key88cdb;
void f2da28(void);
void f377ae(void);
void f37cf1(void);

void fn_193a8(void)
{
    char pal[0x300];            /* esp+0x000: working palette */
    char name[0x0c];            /* esp+0x300: "(B)SBCS.PCX" */
    int flash;                  /* esp+0x328: fn_182d0 a4 toggle */
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
    f1bf94(pal7d850);
    pal_a = 0;
    if (v7ecb4 != 0)
        strcpy(name, "BSBCS.PCX");
    else
        strcpy(name, "SBCS.PCX");
    datfind(name);
    seekdat(telldat() + datsize() - 0x300);
    readdat(pal7d848, 0x300);
    f26ec4(pal7d848);
    f27060((struct rec14 *)name, 2);
    v88b4a = 0;
    p7d714 = p6a980;
    pal_a = f375cd(p6a980, pal_a);
    f37de0();
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
    f32cf8(pal);
    flash = 0;
    f278c4(0);
    v88b9e = 0;
    ticks = 0;
    ticks2 = 0;
    w88b10 = 1;
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
    f377ae();
    if (v7b6dd && (v7b6dc == 0x48 || v7b6dc == 0x50))
        joy88b40 = 0;
    if (netgame == 0) {
        if (v88cce)
            joy88b40 = 1;
        if (v7b6d9)
            key88cd5 = 1;
        if (v7b6db)
            key88cdd = 1;
        if (key88cd3 || key88cd2)
            key88cdd = 1;
        if (key88cd6 || key88cdb)
            key88cd5 = 1;
        goto joined;
    }
    if (netgame == 1) {
        if (v88cce)
            joy88b40 = 1;
        if (v7b6d9)
            key88cd5 = 1;
        if (v7b6db)
            key88cdd = 1;
        if (key88cd3 || key88cd2)
            key88cdd = 1;
        if (key88cd6 || key88cdb)
            key88cd5 = 1;
        if (netgame && v7ec9c == 1 && v88c96 > 7)
            joy88b40 = 0;
        f2da28();
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
    w88b1e = idx;
    if (i > 12) {
        if (flash) {
            fn_182d0(pal, idx, 1, 1, (int)pal7d848);
            flash = 0;
        } else {
            fn_182d0(pal, idx, 1, 0, (int)pal7d848);
            flash = 1;
        }
        f37cf1();
        i = 0;
    }
    if (key88cd5) {
        fn_182d0(pal, idx, 1, 0, (int)pal7d848);
        flash = 0;
        f37cf1();
        v88c96--;
        i = 0;
        if (v88c96 < 6)
            v88c96 = 0xb;
        fn_182d0(pal, v88c96 - 1, 1, 1, (int)pal7d848);
        w88b1e = v88c96 - 1;
        f37cf1();
    }
    if (key88cdd) {
        fn_182d0(pal, idx, 1, 0, (int)pal7d848);
        flash = 0;
        f37cf1();
        v88c96++;
        i = 0;
        if (v88c96 > 0xb)
            v88c96 = 6;
        fn_182d0(pal, v88c96 - 1, 1, 1, (int)pal7d848);
        w88b1e = v88c96 - 1;
        f37cf1();
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
        /* differs: the game zero-extends the comparison into EAX (movzx eax,al, 0x19d0e); our 9.5b
         * keeps this boolean in EBX and adds `mov ebx,eax`. Those +2 bytes are the whole gap (321 vs
         * 319); every other instruction of 0x19c68-0x19da6 matches, only the jumps that cross the
         * extra mov differ (their displacements are 2 smaller). */
        dev = (((short *) cfg77128)[2] == 0);
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
 * -oneatx. Same family as ctl_label's EBX-vs-EAX gap and question 1 (QUESTIONS.md 1, 2). */
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

/* differs: near miss at +8 (6562 vs 6861). The statements are in the game's order - with the
 * `disp(%esp)` and the relocation bytes masked, 71 % of the disassembly rows agree - but Watcom
 * gave our 26 spilled scalars a permutation of the game's 27 slots and kept one local the game
 * spills in a register, so the frame is 4 bytes short. Probe recipe: QUESTIONS.md 5.
 * Frame: 3 char[0x50] at esp+0/0x50/0xa0, short[8] iconx/carstat at 0xf0/0x100, scalars 0x110-. */
void cars_menu(void)
{
    char buf0[0x50], buf1[0x50], buf2[0x50];   /* esp+0x00, esp+0x50, esp+0xa0 */
    short iconx[8], carstat[8];                /* esp+0xf0, esp+0x100 */
    int c110, idle, savepal, slide_up, slide_dn, yrow134, yrow138, yrow13c, boxy, cd15c, flip, stage, boxx;
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
    savepal = pal_a;
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
    capx = (0x140 - (short) (8 * strlen(buf1))) / 2;   /* centred caption x, kept live in ECX (0x1a4a9) */
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
    pal_a = 0x7d00;
    print_at(x2e, capy, buf1, 0);
    boxy = 0x8e;
    print_at(x2e, 0xaf, buf0, 0);
    boxx = 0xec;
    print_at(x2e, 0xbc, buf2, 0);
    pal_a = savepal;
    blink = 0;
    pal_b = 0x7d00;
    voice_play(0x12);

    /* --- phase 1 (0x1a637): the ten frames of each icon alternate until every ent slot of the nine
     * has a frame past 3 (0x1a783, 0x1a7a0); the two box sprites are the pbase / pbase+1 pair. */
blink:
    v7ea70 = ticks;
    pal_c = pal_a;
    setpalette();
    if (ents[0].frame >= 2) {                   /* 0x1a657: nothing is drawn before frame 2 */
        for (i = 0; i < 8; i++) {
            spr_x = ents[i].x;
            spr_y = ents[i].y;
            spr_h = 0x26;
            spr_w = 0x1c;
            sprptr = faces[ents[i].id].img[0];
            if (carstat[i] != 1)
                sprptr += 0x10a0;               /* the frame-10 variant (0x1a6b8) */
            drawsprite();
        }
        spr_x = ents[8].x;
        spr_y = ents[8].y;
        spr_h = 0x26;
        spr_w = 0x1c;
        sprptr = sp6a878[pbase];
        drawsprite();
    }
    ents_draw(9);
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (pal_a == 0)                             /* 0x1a74e */
        pal_a = 0x3e80;
    else
        pal_a = 0;
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
    pal_c = pal_a;
    setpalette();
    if (longwait == 0) {
        v7d878 = 0;
        p7d838 = tab6e5b0[cd];
        p7d764 = p7d838 + 1;
        y = yrow134;
        for (j = 0; j < 2; j++) {               /* 0x1a912: the two name rows, 0xc apart */
            v88c2e = p7d764[-1];
            v88c10 = p7d764[0];
            p7d838 += 2;
            pname_desc(car);
            wtext(col0x - 1, y, &mnames[0x3c * j], 1);
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
    drawsprite();
    ents_show(-1, 8, 0, 0, 0);
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (pal_a == 0)                             /* 0x1a9d8 */
        pal_a = 0x3e80;
    else
        pal_a = 0;
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
        f377ae();
        joy88b46 = (key88cd0 == 0x3b);          /* F1 on the local keyboard (0x1ab47) */
        if (joy88b46 != 0) {
            if (v88b7e == 0 && netgame != 1)
                joy88b46 = 0;
            if (v88b7e == 1 && netgame != 2)
                joy88b46 = 0;
        }
        if (joy88b42 != 0)
            joy88b40 = 1;
        if (v88cce != 0)
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
        f2da28();
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
    pal_c = pal_a;
    setpalette();
    ents_show(-1, 8, 0, 0, 0);
    spr_x = boxx;
    spr_h = 0x26;
    sprptr = sp6a878[pbase + flip];
    spr_y = boxy;
    pname_desc(car);
    y = yrow138;
    for (j = 0; j < 2; j++) {                   /* the same two rows, unscaled (0x1ae75) */
        wtext(col0x - 1, y, &mnames[0x3c * j], 0);
        y += 0xc;
    }
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (pal_a == 0)                             /* 0x1aeb9 */
        pal_a = 0x3e80;
    else
        pal_a = 0;
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
            save_a = pal_a;
            save_b = pal_b;
            save_c = pal_c;
            pal_c = 0xbb80;
            pal_a = pal_d;
            pal_b = pal_d;
            setpalette();
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
            pal_b = 0xbb80;
            pal_a = pal_d;
            pal_c = pal_d;
            setpalette();
            pal_b = save_b;
            pal_c = save_c;
            pal_a = save_a;
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
        f377ae();
        if ((twoplayer != 0 || splitscreen != 0) && v88b7e == 1) {
            if (cfg7712c == 0)
                joy88b40 = joy88b44;
            else
                joy88b40 = v7b6df[12 * v88b7e];
        }
        if (joy88b42 != 0)
            joy88b40 = 1;
        if (v88cce != 0)
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
    pal_c = pal_a;
    setpalette();
    spr_x = boxx;
    spr_h = 0x26;
    spr_w = 0x1c;
    spr_y = boxy;
    sprptr = sp6a878[pbase + add158];
    drawsprite();
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
    drawsprite();
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (pal_a == 0)                             /* 0x1bb16 */
        pal_a = 0x3e80;
    else
        pal_a = 0;
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
/* differs: near miss, 196 bytes (the game's size exactly), 13 instructions off, all of them the
 * same Watcom choice: it keeps a constant 0 in edi across the loop (`push edi`, `xor edi,edi`,
 * `cmp splitscreen,di`, `mov edi,ebx`, `mov edi,edx`) where the game materialises each zero
 * (`cmpw $0,splitscreen`, `xor ebx,ebx`, `xor edx,edx`). Every statement, jump and loop bound
 * otherwise agrees byte for byte; 13 C shapes (declaration orders, `register`, goto vs break vs
 * continue, do/while vs while; k1-k4 in analysis/shapes/SHAPES.C) all give the same 13. Same
 * family as QUESTIONS.md 2 and 3. */
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
/* differs: near miss, 99 of the game's 125 bytes, and only two things are wrong. (1) The game
 * loads nplayers into esi (0x1bf04) before v7eca4 into ebx (0x1bf0b); our build loads v7eca4
 * first in every shape tried (initializer, statement, declaration order). (2) The game keeps the
 * whole store-and-continue block inside each arm of the test (`movw $1,f64; inc edx; mov face` +
 * `cmp si,dx; jg exit; jmp head` at 0x1bf36-0x1bf4c and again at 0x1bf5b-0x1bf6f); our build
 * merges the two arms into one tail. 27 shapes scored (m1-m4 kept in analysis/shapes/SHAPES.C);
 * the best is 14 instruction diffs and none of them avoids the merge. */
void fn_1bf00(void)
{
    short n, cnt, i, val;

    n = nplayers;                       /* 0x1bf04 (the game's first load) */
    cnt = v7eca4 + 1;                   /* 0x1bf0b */
    if (cnt > 0x15)
        cnt = 0x15;
    i = 1;                              /* 0x1bf1e */
    do {
        if (cnt > 0) {                  /* 0x1bf23 */
            val = v7ec6c;               /* 0x1bf2e */
            cnt--;
        } else {
            val = v7ec70;               /* 0x1bf54 */
        }
        players[i].f64 = 1;             /* 0x1bf36 / 0x1bf5b */
        players[i].face = val;          /* 0x1bf40 / 0x1bf65 */
        i++;
    } while (i <= n);
    nplayers = n;                       /* 0x1bf71 */
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
/* differs: near miss, 312 of the game's 313 bytes and four instruction diffs, all one thing:
 * the game keeps `edi = s` off the loop head (0x1ca83 starts straight at `push edi`, edi is still
 * s from the `pop edi` at 0x1ca74) and re-materialises it on the back edge at 0x1cabf; our build
 * puts `mov ebp,edi` at the loop head instead, which shifts the `jae`/`jmp` displacements (0x1cab8,
 * 0x1cabf). t1-t6 in analysis/shapes/SHAPES.C keep six of the 21 shapes scored; t1 (this one) is the
 * best at 4 diffs, and the pre-test `while (strlen(s) < 10) strcat(s, " ")` form is 8, so the game's
 * loop really is entered twice (findings/scores.md). */
void fn_1c994(char *s)                  /* arg1 = eax */
{
    char mth[12][4] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    char num[16];
    struct dostime t;
    char *src;
    char c;

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
    while (1) {
        strcat(s, src);                 /* 0x1ca83 */
        if (strlen(s) >= 10)            /* 0x1caa9-0x1cab8: cmp ecx,0xa; jae exit */
            return;
        src = " ";                      /* 0x1caba */
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
 * differs: 67 instructions of 1944 (same size, tools/wc/fndiff.py). All five are block shape:
 * the third centred line swaps `mov eax,0x140` with `lea edx,[ecx*8]` and sets ecx before the
 * spr_y load (0x1c35c, 0x1c37a); the twoplayer/splitscreen record is recomputed into ESI with the
 * +0x46 name in EDX and its shared name block placed *before* the netgame test, which jumps back
 * to it at 0x1c542 (ours tests first, 0x1c53f); the single-player chain tests 1 then 2 and jumps
 * forward to both blocks (0x1c65d-0x1c667); the print loop's first row pointer is
 * `mov edi,esp; add edi,0x50` (0x1c715); and the 60-tick wait keeps the snapshot in EAX and the
 * fresh ticks in EDX (0x1c821-0x1c831). */
extern char ctl_names77432[];           /* 0x77432, inside cfg77128 (0x77128 + 0x30a) */
extern short cfg77128w, cfg7712a;       /* 0x77128, 0x7712a: shorts inside cfg77128 */
void f27920(int a);                     /* 0x27920: called with 0 wherever f278c4 is */

void fn_1c0f4(char *buf)                /* arg1 = eax (0x1c100: mov ebp,eax) */
{
    int vals[5];
    char rows[12][80];
    char *p, *q;
    short x;
    int i, n, t, v;

    if (netgame != 0)                                   /* 0x1c102 */
        vals[3] = 0;                                      /* 0x1c10c */
    else
        vals[3] = v88b7e;                                 /* 0x1c119-0x1c126 */
    spr_x = 0;                                          /* 0x1c127 */
    spr_y = 0;
    spr_h = 0x140;
    spr_w = 0xc8;
    v88ce9 = 0x2c;                                      /* 0x1c14b */
    vals[4] = pal_a;                                    /* 0x1c152-0x1c166 */
    v88ce8 = 0x26;
    vals[2] = pal_d;                                    /* 0x1c167 */
    pal_a = 0xbb80;
    f371e4();                                           /* 0x1c17e */
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
    x = (0x140 - (int)strlen(rows[10]) * 8) / 2;        /* 0x1c352-0x1c371 */
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
        if (netgame == 0 || (v = vals[vals[3]]) <= 1) { /* 0x1c538-0x1c58b */
            strcat(rows[5], q);                         /* 0x1c549 */
            strcat(rows[6], p);                         /* 0x1c5d0 */
        } else if (v == 2) {                            /* 0x1c58d */
            strcat(rows[5], "BUTTON3");                 /* 0x1c59e */
            strcat(rows[6], "BUTTON4");                 /* 0x1c5d0 */
        }
        strcpy(rows[7], "");                            /* 0x1c5f6-0x1c606 */
        n = 8;                                          /* 0x1c5fb */
        if (vals[3] != 0 && netgame != 0)               /* 0x1c607-0x1c619 */
            strcpy(rows[8], "1  - 6  ARE YOUR GIGGLE KEYS!");   /* 0x1c635 */
        else
            strcpy(rows[8], "F1 - F6 ARE YOUR GIGGLE KEYS!");   /* 0x1c61b */
    } else {
        v = vals[vals[3]];                              /* 0x1c64f-0x1c656 */
        if (v <= 1) {                                   /* 0x1c65d */
            strcat(rows[5], p + 0x38);                  /* 0x1c676 */
            strcat(rows[6], p + 0x46);                  /* 0x1c6e6 */
        } else if (v == 2) {                            /* 0x1c662 */
            strcat(rows[5], "BUTTON3");                 /* 0x1c6b4 */
            strcat(rows[6], "BUTTON4");                 /* 0x1c6e6 */
        }
    }
    for (i = 1; i <= n; i++) {                          /* 0x1c70c-0x1c746 */
        spr_x = 0x10;
        print_at(0x10, spr_y, rows[i], 0);
        spr_y += 0xa;
    }
    strcpy(rows[10], "PRESS FIRE TO LEAVE HELP");       /* 0x1c748 */
    spr_y = 0xba;                                       /* 0x1c763 */
    x = (0x140 - (int)strlen(rows[10]) * 8) / 2;        /* 0x1c76c-0x1c78b */
    spr_x = x;
    print_at(x, spr_y, rows[10], 0);                    /* 0x1c7a4 */
    waitretrace();                                      /* 0x1c7a9 */
    if (key88ccd) {                                     /* 0x1c7ae */
        screenshot();                                   /* 0x1c7b7 */
        if (key88ccd)
            do
                ;
            while (key88ccd);
    }
    while (key88cd0 == 0x3b)                            /* 0x1c7ce: wait for F1 to come up */
        ;
    f278c4(0);                                          /* 0x1c7d9 */
    if (netgame == 0) {
        f27920(0);                                      /* 0x1c7ea */
    } else {
        v88cf0 = 0;                                     /* 0x1c7f1 */
        v88cf3 = 0;
        f27920(0);                                      /* 0x1c801 */
        net88c14 = net_key(v88cf0, v88cf3, 0);          /* 0x1c816-0x1c81f */
        t = ticks;                                      /* 0x1c821 */
        while (ticks - t < 60)
            ;
    }
    pal_a = vals[2];                                    /* 0x1c833-0x1c83c */
    waitretrace();                                      /* 0x1c840 */
    if (key88ccd) {                                     /* 0x1c845 */
        screenshot();                                   /* 0x1c84e */
        if (key88ccd)
            do
                ;
            while (key88ccd);
    }
    pal_d = vals[2];                                    /* 0x1c865-0x1c87c */
    pal_a = vals[4];
}

/* ---- 0x1c88c: the same palette-page flip and tick wait as modem(), around msgbox. Nothing in
 * the image refers to it: no E8/E9 rel32 target in the code object and no absolute dword for it
 * in either object (scan, see findings/menus.md), so it is dead code the linker kept. Rebuild
 * byte-identical, fixups included. */
void fn_1c88c(char *s, int flag)        /* arg1 = eax, arg2 = edx */
{
    if (flag) {                                         /* 0x1c892 */
        save_a = pal_a;                                 /* 0x1c896 */
        save_b = pal_b;
        save_c = pal_c;
        pal_c = 0xbb80;
        pal_a = pal_d;                                  /* 0x1c8c9 */
        pal_b = pal_d;
        setpalette();                                   /* 0x1c8d5 */
    }
    msgbox(s, 0);                                       /* 0x1c8de */
    if (flag) {                                         /* 0x1c8e3 */
        ticks = 0;
        ticks2 = 0;
        while (ticks <= 300)
            ;
        pal_b = 0xbb80;                                 /* 0x1c917-0x1c92e */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();                                   /* 0x1c935 */
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
    }
}

/* ---- 0x1c968: saves the palette over pal7d850 and calls f1bf94, setting 0x88bda. Also
 * unreferenced in the image (same scan as fn_1c88c). Rebuild byte-identical. */
extern short v88bda;

void fn_1c968(void)
{
    pal_a = 0;                                          /* 0x1c96d */
    palptr = (unsigned char *)pal7d850;                 /* 0x1c968, 0x1c976 */
    getpalette();                                       /* 0x1c97b */
    f1bf94(pal7d850);                                   /* 0x1c985 */
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
 * `pal_a = pal_a ? 0 : 0x3e80` form compiles to a value select in EAX, which the game never
 * emits (GOTCHAS). */
void enter_name(char *name, char *heading, int n)   /* arg1 = eax, arg2 = edx, arg3 = ebx */
{
    char prompt[0x28];
    union REGS r;
    struct SREGS s;
    short cx, y, lim, nx, x, cy, xc, i;     /* slots: the game's permutation, QUESTIONS.md 9 */
    char ch;

    pal_c = 0x7d00;                                 /* 0x1cb55 */
    pal_b = pal_a;
    setpalette();                                   /* 0x1cb6c */
    xc = center_x(strlen(heading) * 11);            /* 0x1cb74-0x1cb8c (both inlined) */
    wtext(xc, 0xa, heading, 0);                     /* 0x1cba5 */
    strcpy(prompt, "PLEASE ENTER NAME..");          /* 0x1cbbf, the literal 0x60d8c */
    x = 0xa;
    y = 0x60;
    print_at(x, y, prompt, 0);                      /* 0x1cbce */
    f37de0();                                       /* 0x1cbd3 */
    if (key88ccd) {                                 /* 0x1cbd8 */
        screenshot();                               /* 0x1cbe1 */
        if (key88ccd)
            while (key88ccd)
                ;
    }
    if (pal_a == 0)                                 /* 0x1cbf8 */
        pal_a = 0x3e80;
    else
        pal_a = 0;
    f32cf8(v7d85c);                                 /* 0x1cc1f */
    f278c4(0);                                      /* 0x1cc2a */
    r.x.edx = oldkbd_off;                           /* 0x1cc38 */
    r.x.eax = 0x2509;                               /* 0x1cc42 */
    s.ds = oldkbd_seg;                              /* 0x1cc4a */
    s.es = 0;                                       /* 0x1cc54 */
    int386x(0x21, &r, &r, &s);                      /* 0x1cc5b */
    kbd_off();                                      /* 0x1cc60 */
    pal_b = 0x7d00;                                 /* 0x1cc65 */
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
        pal_c = pal_a;                              /* 0x1ccd5 */
        setpalette();                               /* 0x1ccdf */
        sprptr = gn7eb2c;                           /* 0x1cce4 */
        spr_h = 0xf;
        spr_x = cx;
        spr_w = 0xd;
        spr_y = cy;
        drawsprite();                               /* 0x1cd16 */
        wtext(xc, 0xa, heading, 0);                 /* 0x1cd20 */
        print_at(x, y, prompt, 0);                  /* 0x1cd30 */
        print_at(nx, y, name, 0);                   /* 0x1cd40 */
        f37de0();                                   /* 0x1cd45 */
        if (key88ccd) {                             /* 0x1cd4a */
            screenshot();                           /* 0x1cd53 */
            if (key88ccd)
                while (key88ccd)
                    ;
        }
        if (pal_a == 0)                             /* 0x1cd6a */
            pal_a = 0x3e80;
        else
            pal_a = 0;
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
void fn_3542c(int n);                 /* 0x3542c: builds a "WACKY" file name (strcat of 0x7c158,
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
 * 5. `movzx edx, word [pal_a]` at 0x1ce57 where the game has `mov dx`: the f375cd EDX parameter
 *    trap (GOTCHAS). Declaring pal_a `short` fixes this site but costs one of the 87 matches. */

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
    int k;                            /* as short k it takes a stack slot and the frame grows to
                                     * 0x5c; as int it stays in ECX like the game's 0x1cf8c */
    int better;                       /* 0x1cf2f/0x1cf44/0x1cf4d: the game's temp is ECX, not CX */
    struct score *dst, *src, *p;

    f27060((struct rec14 *)"LEAD.PCX", 2);     /* 0x1ce1c-0x1ce26, the string 0x60dc4 */
    palptr = pal7d850;                /* 0x1ce2b-0x1ce30 */
    getpalette();                     /* 0x1ce35 */
    f1bf94(pal7d850);                 /* 0x1ce3a-0x1ce44 */
    v88b4a = 0;                       /* 0x1ce49 */
    p7d714 = p6a980;                  /* 0x1ce52-0x1ce5e */
    pal_a = f375cd(p6a980, pal_a);    /* 0x1ce57-0x1ce6c */
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
            sc[i] = *(short *)(tbl77758 + (v88bb6 + v7ecb4) * 0x450 + 0x2e0 + i * 0x2e + 4);
        break;
    }
    better = 1;                       /* 0x1cf2f */
    for (i = 0; i <= 3; i++)          /* 0x1cf3c-0x1cf4b */
        if (v7ed5c >= sc[i])
            better = 0;
    if (better)
        sc[3] = v7ed5c - 1;           /* 0x1cf51-0x1cf57: guarantees the loop below takes a slot */
    fn_1c994(stamp);                  /* 0x1cf5b-0x1cf67 */
    for (j = 0; j <= 3; j++) {        /* 0x1cf72, the inc test at 0x1d0b9-0x1d0c3 */
        if (v7ed5c >= sc[j]) {        /* 0x1cf77-0x1cf7a */
            if (j != 3)               /* 0x1cf80-0x1cf86 */
                for (k = 3; k > j; k--) {   /* 0x1cf8c-0x1cf91: k in ECX */
                    fn_1cad0(which, k);       /* 0x1cf9b-0x1cf9f */
                    dst = (struct score *)v7d8a8;             /* 0x1cfa9 */
                    fn_1cad0(which, k - 1);   /* 0x1cfa4-0x1cfaf */
                    src = (struct score *)v7d8a8;             /* 0x1cfb4 */
                    dst->score = src->score;  /* 0x1cfb9-0x1cfbd */
                    dst->row = src->row;      /* 0x1cfc1-0x1cfc4 */
                    dst->player = src->player;                /* 0x1cfca-0x1cfd1 */
                    strcpy(dst->name, src->name);             /* 0x1cfd5-0x1cff2, +0x1a first */
                    strcpy(dst->stamp, src->stamp);           /* 0x1cff3-0x1d01d, +6 second */
                }
            fn_1cad0(which, j);       /* 0x1d02c-0x1d036 */
            p = (struct score *)v7d8a8;                       /* 0x1d03b */
            p->score = v7ed5c;        /* 0x1d040-0x1d047 */
            p->row = v88bb6 + v7ecb4; /* 0x1d04b-0x1d05d */
            p->player = myplayer;     /* 0x1d060-0x1d06a */
            strcpy(p->stamp, stamp);  /* 0x1d06e-0x1d08b */
            strcpy(p->name, name);    /* 0x1d08c-0x1d0b6 */
            break;                    /* 0x1d0b7 */
        }
    }
    fn_3542c(v7ee5a);                 /* 0x1d0c9-0x1d0d0 */
    if (cursong != menusong)          /* 0x1d0d5-0x1d0e0 */
        music_start(menusong, 1);     /* 0x1d0e2-0x1d114, inlined by -oe like the game's */
    fn_1d704(v88bb2, v88bb6 + v7ecb4);                        /* 0x1d119-0x1d12d */
}

/* 0x1d13c, 516 bytes. Called from fn_1f858 at 0x2108f with the "class drivers" menu choice
 * (v88c80 1 AMATEUR / 2 PRO / 3 CHAMP, 0x21074-0x2108f; choice 4 goes to 0x1d340).
 * Each row shows that class's record table: fn_1d704(class, v7ecb4 + choice), so v7ecb4 (0 regular,
 * 3 bonus pack, zones_menu) selects the group of tbl77758. v7ecb4 != 0 prefixes the item names with
 * " BONUS" (the names are built by strcat on top of that prefix). */
/* differs: near miss, 504 of the game's 516 bytes; one diff block. The game 0x1d178-0x1d196 is
 * `mov dl,[0x600ac]` + four `mov byte [mnames slot],dl` + the join jmp; ours 0x1d178-0x1d18c is
 * `mov al,[0x600ac]` + three `mov byte [slot],al` + that jmp, and the game has 2 bytes of 4-byte
 * alignment padding (0x1d33e) that ours does not. Ours keeps the pooled "" byte in EAX (the AL-only
 * short forms `a0`/`a2`, 5 bytes) and the game in EDX (`8a 15`/`88 15`, 6 bytes each); with EAX, -oe
 * tail-merges this arm's fourth strcpy with the identical one that ends the else arm (the store at
 * the join serves both arms), with EDX the game keeps a copy in each arm. Writing it as `if (v7ecb4 != 0) {BONUS
 * ...} else {"" x4}` does keep four stores in one arm (514 bytes, after which the only aligned diffs
 * are AL-vs-DL) but puts the BONUS arm in the fall-through position, so the branch is `je` where the
 * game has `jne 0x1d198` (0x1d176) - two changes, not one. The sibling 0x1d340 has our merged shape
 * in the game itself: its if arm stores three (0x1d386-0x1d390) and the fourth sits at the join,
 * 0x1d3c6, loaded into AL. */
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
            strcpy(&mnames[0], " BONUS");
            strcpy(&mnames[0x3c], " BONUS");
            strcpy(&mnames[0x78], " BONUS");
            strcpy(&mnames[0xb4], "");
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
 * f2e4b8(metal + v7ecb4, track, 0) between two v88cec = 0, then reloads CHECK.PCX. */
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
            f2e4b8(sel + v7ecb4, track, 0);         /* 0x1d6b3-0x1d6ca */
            v88cec = 0;                             /* 0x1d6cf */
            v88bda = 1;                             /* 0x1d6dd */
            f27060((struct rec14 *)"CHECK.PCX", 0); /* 0x1d6d6-0x1d6e6 */
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
 * 2. `mov dx,[pal_a]` vs ours `movzx edx` (0x1d73e): the f375cd EDX parameter trap (GOTCHAS).
 * The prototype above is unprototyped (`void fn_1d704();`): a `short which` prototype makes both
 * callers sign-extend (race_records_menu 504 -> 472 bytes), so the game's callers saw none. */
void fn_1d704(short which, int row)
{
    char cls[3][40] = { "AMATEUR CLASS", "PRO CLASS", "CHAMP CLASS" };  /* 0x10184, 0x1d713 */
    char num[0x14];                           /* esp+0x78, itoa 0x1d9d8 */
    short i, y, ystamp, ypts;                 /* edi, esi, esp+0x98, esp+0x90: i first keeps the
                                               * game's register order (21 -> 17 diffs) */
    short sx;                                 /* esp+0x8c */

    f27060((struct rec14 *)"LEAD.PCX", 2);    /* 0x1d71f-0x1d72b */
    v88b4a = 0;
    p7d714 = p6a980;
    pal_a = f375cd(p6a980, pal_a);            /* 0x1d739-0x1d758 */
    pal_c = 0x7d00;
    pal_b = pal_a;
    setpalette();                             /* 0x1d766 */
    f37de0();
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    if (pal_a == 0)                           /* 0x1d790 */
        pal_a = 0x3e80;
    else
        pal_a = 0;
    f278c4(0);                                /* 0x1d7b0 */
    pal_b = 0x7d00;
    pal_c = pal_a;
    setpalette();                             /* 0x1d7d1 */
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
        drawsprite();                         /* 0x1d92f */
        spr_x += 0x30;
        print_at(spr_x, ystamp, v7d8a8 + 6, 0);   /* the stamp, 0x1d94c */
        sprptr = sp7c800[(row - (v7ecb4 + 1)) * 2];
        spr_h = 0x1c;
        spr_w = 0x17;
        spr_x += 8;
        spr_y = y - 7;
        drawsprite();                         /* 0x1d98e */
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
    f37de0();                                 /* 0x1da4d */
    if (key88ccd) {
        screenshot();
        while (key88ccd)
            ;
    }
    f27920(1);                                /* 0x1da77 */
    if (pal_a == 0)
        pal_a = 0x3e80;
    else
        pal_a = 0;
    f278c4(0);                                /* 0x1da9c */
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

/* differs: near miss, the game calls com_getc at 0x1dc7f where -oe inlines ours (the com_getc trap
 * at net_end above, QUESTIONS 6); everything before it is identical. */
/* ---- 0x1dae8, 437 bytes: opens the serial link (caller 0x20943 in fn_1f858). Pauses the music with
 * a "MUSIC PAUSED" box, shows "INTERBYTE DELAY IS SET TO n" when comdelay is set (then
 * lib_40ff0(comtask, 1000)), opens COM v7ec50 with the port/IRQ of its comports[] record and the
 * baud v7ebd4, waits 0x88 ticks (wait_or_key, inlined), drains the receiver and sets comopen. */
struct comport { int port; int irq; int f08; };   /* 12 bytes, 0x77240 (0x1db4b-0x1db61) */
extern struct comport comports[];
extern int v7ec50;                    /* the COM port number: index of comports, com_open's 4th arg */

void link_open(void)
{
    char num[0x2c];                   /* esp+0x00: itoa's buffer */
    char msg[0x28];                   /* esp+0x2c */
    int port, irq;                    /* esp+0x54, esp+0x58: dword loads and stores (0x1db52-0x1db69),
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
        lib_40ff0(comtask, 1000);     /* 0x1dc1f */
    }
    com_open(port, irq, v7ebd4, v7ec50);   /* 0x1dc3c */
    wait_or_key(0x88);                /* 0x1dc41-0x1dc7d, inlined */
    while (com_getc() != -1)          /* 0x1dc7f */
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

    save_a = pal_a;                   /* fn_1c068, inline */
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
    msgbox("WAITING FOR CONNECT", 0);
    ok = com_waitfor("CONNECT");      /* 0x1dcf6 */
    if (ok) {
        pal_b = 48000;                /* fn_1c0ac, inline */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
        save_a = pal_a;               /* fn_1c068, inline */
        save_b = pal_b;
        save_c = pal_c;
        pal_c = 48000;
        pal_a = pal_d;
        pal_b = pal_d;
        setpalette();
        msgbox("CONNECTED", 0);       /* 0x1dd86 */
        ticks = 0;
        ticks2 = 0;
        do {
            if (aborted == 1 && connected) {
                sendfail = 1;
                break;
            }
        } while (ticks < 0x88);
        pal_b = 48000;                /* fn_1c0ac, inline */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
        return 1;
    }
    if (sendfail || !ok) {            /* 0x1de14-0x1de1f */
        pal_b = 48000;                /* fn_1c0ac, inline */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
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
/* differs: near miss, the game calls com_getc at 0x1dfc2 where -oe inlines ours (QUESTIONS 6);
 * everything before it is identical. */
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
    save_a = pal_a;                   /* fn_1c068, inline */
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
    msgbox(msg, 0);                   /* 0x1dfbd */
    while (com_getc() != -1)
        ;
    wait_or_key(0x110);               /* 0x1dfcf-0x1e00b, inlined */
    sendstr(cmd);                     /* 0x1e00f */
    wait_or_key(0x110);
    pal_b = 48000;                    /* fn_1c0ac, inline */
    pal_a = pal_d;
    pal_c = pal_d;
    setpalette();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
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
    save_a = pal_a;                   /* fn_1c068, inline */
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    setpalette();
    msgbox("WAITING FOR CALL....", 0);
    while (1) {                       /* `for (;;)` + `if (x)` costs one instruction */
        if (com_waitfor("RING") != 0) {   /* 0x1e14a; the failed wait goes to 0x1e234, after the return */
        pal_b = 48000;                    /* fn_1c0ac, inline */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
        save_a = pal_a;                   /* fn_1c068, inline */
        save_b = pal_b;
        save_c = pal_c;
        pal_c = 48000;
        pal_a = pal_d;
        pal_b = pal_d;
        setpalette();
        msgbox("ANSWERING CALL...", 0);   /* 0x1e1da */
        sendstr("ATA");
        pal_b = 48000;                    /* fn_1c0ac, inline */
        pal_a = pal_d;
        pal_c = pal_d;
        setpalette();
        pal_b = save_b;
        pal_c = save_c;
        pal_a = save_a;
        return wait_connect();
        }
        if (sendfail != 0)            /* 0x1e234 */
            break;
    }
    pal_b = 48000;                    /* fn_1c0ac, inline (0x1e241) */
    pal_a = pal_d;
    pal_c = pal_d;
    setpalette();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
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
 * 2. a `mov edx,ebx` before the second f375cd (0x1e648) that the game leaves out (EDX is 0 from
 *    0x1e609); `f375cd(p6a978, 0)` and `pal_a = f375cd(p6a978, pal_a)` both keep it.
 * 3. `mov dx,[pal_a]` at 0x1e472: the f375cd EDX parameter trap (GOTCHAS). */
/* ---- 0x1e2e4, 1013 bytes: the demo races. mode 3 when the menus sit idle (v7ed0c >= 500, callers
 * 0x1faaf/0x1fb04), mode 1 from a menu entry (0x20f48). The mode picks the picture set ("OR"/"INT",
 * rec7c208[1..n] = "<set><i>.PCX" for n = 11/8/1) and the starting group/class (v7ec5c, v7ec58).
 * Then, with v7ee58/v7ee5a saved and forced to 2/1, it runs one single-player race (0x2fd8c with
 * countplays = 1) per pass, rotating the driver v7ec60 through 0-7, until a race sets v7ec68 or
 * v7ec58 passes 5. */
extern short w88c98, w88c9a, w88bce, w88c08;
extern int v7ec5c, v7ec58, v7ec60, v7ec90, v7ec68;
void f2fd8c(void);

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
    p7d714 = p6a978;
    pal_a = f375cd(p6a978, pal_a);    /* 0x1e49d */
    v7ecb4 = 0;
    v88c20 = v7ee58;
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
        f2fd8c();                     /* 0x1e5d0: the race */
        countplays = 0;
        voices_reset();
        if (++v7ec60 > 7)             /* 0x1e5e0 */
            v7ec60 = 0;
        palptr = pal7d850;
        getpalette();
        f1bf94(pal7d850);
        f27060((struct rec14 *)"CHECK.PCX", 0);   /* 0x1e615 */
        if (v7ec68 != 0)              /* 0x1e61a */
            break;
        if (++v7ec58 > 5)             /* 0x1e626 */
            break;
        v88b4a = 0;
        pal_a = 0;
        p7d714 = p6a978;
        f375cd(p6a978, 0);            /* 0x1e64d, result unused; EDX is still 0 from 0x1e609 */
        pal_c = 0x3e80;
        pal_a = 0;
        pal_b = 0;
        setpalette();
        f37de0();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        f32cf8(v7d85c);               /* 0x1e695 */
    }
    v7d998 = 0;                       /* 0x1e69f */
    v88bda = 1;
    v7ee58 = s58;
    v7ee5a = s5a;
    v88c20 = v7ee58;                  /* re-read as the setup does: movsx eax,di (0x1e6c0) */
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

/* differs: near miss, 1921 of the game's 1922 bytes, 402 vs 403 instructions, two causes:
 * 1. the frame wait (0x1ecba): the game loads both operands, `mov eax,[v7e724]; mov edx,[ticks];
 *    sub edx,eax`, ours `mov ebx,[ticks]; sub ebx,[v7e724]`. cars_menu's identical-looking
 *    `ticks - v7ea70` loop (0x1a9f7) matches, but there no call sits in the loop. Tried: volatile
 *    v7e724 (6 blocks), `for (;;)` + break, `0xe > ...`, `ticks < v7e724 + 0xe` and a leading
 *    `while` (all worse or equal).
 * 2. `mov dx,[pal_a]` at 0x1e816: the f375cd EDX parameter trap.
 * The levers that did matter: `k = kind[i]` read first and compared as a register, `spr_h = vol * 4`
 * before the other three sprite stores, `ys[i]` before `spr_x = 10`, `vol < 0` (not < 1) and
 * `spr_x + (spr_h - 2)`. */
/* ---- 0x1e760, 1922 bytes: the SOUND VOLUMES screen (caller 0x21198 in fn_1f858). Rows are indexed
 * from 1 like mrows: MUSIC when musicready, SOUND FX and ENGINE FX when sfxsoft, kind[] 1/2/3.
 * Each row's volume is a bar of (vol / 5) clamped to 1..0x33, times 4 pixels (f371e4); a blinking
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
    p7d714 = p6a978;
    pal_a = f375cd(p6a978, pal_a);    /* 0x1e824 */
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
    for (i = 1; i <= n; i++) {        /* 0x1e9c3-0x1ea63 */
        ys[i] = spr_y;
        spr_x = 10;
        textx = spr_x + strlen(rows[i]) * 8;
        print_at(spr_x, spr_y, rows[i], 0);
        spr_y += 0x14;
    }
    pal_c = 0xbb80;                   /* 0x1ea65 */
    pal_b = pal_a;
    setpalette();
    sel = 1;
    ticks2 = 0;
    v7eaf8 = -1;
    v7eb08 = 0;
    pal_b = pal_c;
    if (sfxsoft) {                    /* 0x1eaae */
        fn_1e6dc();
        cars[0].sndhandle = -1;
        cars[1].sndhandle = -1;
        engine_start();
    }
    for (;;) {
        v7e724 = ticks;               /* 0x1eada */
        pal_c = pal_a;
        setpalette();
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
            v88ce9 = 0x2c;
            v88ce8 = 0x26;
            f371e4();                 /* 0x1ebc2: the bar */
            if (i == sel)
                barx = spr_x + (spr_h - 2);
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
        drawsprite();                 /* 0x1ec64 */
        f37de0();
        if (key88ccd) {
            screenshot();
            while (key88ccd)
                ;
        }
        if (pal_a == 0)
            pal_a = 0x3e80;
        else
            pal_a = 0;
        do {
            if (sfxsoft)
                fn_1e6dc();
        } while (ticks - v7e724 < 0xe);   /* 0x1ecac-0x1ecca */
        ticks2++;
        if (key88cd0 == 1)            /* 0x1ecd2 */
            break;
        v7d8a4 = v7d8ac;
        ctl_setup(0);
        f377ae();                     /* 0x1ecf0 */
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
        if (key88cd6 || key88cdb) {   /* up, 0x1ee50 */
            blink = 0;
            if (sel > 1)
                sel--;
        }
        if (key88cd3 || key88cd2) {   /* down, 0x1ee73 */
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

/* differs: near miss, 1173 of the game's 1164 bytes, 235 vs 233 instructions, three causes:
 * 1. slots: the game has dir at esp+0x08 and y at esp+0x10, ours the other way round (QUESTIONS 9/10
 *    class); 48 declaration orders tried, `dir, page, first, y` is the closest (52 -> 47 scored).
 * 2. `mov edx, 1` before the first credits_page (0x1f01f): the game reuses EDX = first from the
 *    f375cd call. A void alias of f375cd (no return register) does not remove it.
 * 3. scheduling of the palette restore and the face pointer around setpalette (0x1f0d0-0x1f10e).
 * Levers that did matter: `int save` (movzx), `if (dir != 0)` arms in both places, and `++face`
 * repeated in each direction arm. */
/* ---- 0x1efb0, 1164 bytes: the credits (caller 0x21099 in fn_1f858). Up to five pages of 12 lines
 * (credits_page); on each, a driver's face (faces[face].img[frame], frames 0-7) walks along the
 * printed lines - right along one line, left along the next, skipping empty lines and switching to
 * the next driver at each line - then runs in place for 0x88 ticks2 or until a key or button.
 * ESC (key88cd0) leaves at any point. */
void credits_screen(void)
{
    int dir, page, first, y;          /* esp+0x08, esp+0x00, esp+0x0c, esp+0x10: this order is the closest */
    int k, x, face, frame;            /* EDX, EBX, ESI, ECX */
    int save;                         /* EDI: movzx loads (0x1efd2) */

    page = 1;
    first = 1;
    save = pal_a;
    f27060((struct rec14 *)"CHECK.PCX", 2);   /* 0x1efd9 */
    v88b4a = 0;
    pal_a = 0x7d00;
    p7d714 = p6a980;
    f375cd(p6a980, first);
    pal_a = 0xbb80;
    pal_b = 0x7d00;
    pal_c = 0xbb80;
    setpalette();
    face = 0;
    credits_page(12, first);          /* 0x1f02b */
    pal_a = save;
    for (;;) {
        k = 1;                        /* 0x1f037 */
        x = cboxes[1].x2;
        frame = 0;
        dir = 1;
        for (;;) {
            y = cboxes[k].y - 10;     /* 0x1f049 */
            ticks = 0;
            save = pal_a;
            pal_a = 0xbb80;
            if (dir != 0)
                spr_x = x;
            else
                spr_x = x + 0x26;
            spr_h = 8;
            spr_y = cboxes[k].y;
            spr_w = 8;
            sprptr = p6a97c;
            blit372c7();              /* 0x1f0bd */
            pal_a = 0x7d00;
            f373ee();
            pal_a = save;
            pal_c = save;
            pal_b = 0x7d00;
            setpalette();
            spr_h = 0x26;
            spr_w = 0x1c;
            spr_y = y;
            sprptr = faces[face].img[frame];
            spr_x = x;
            drawsprite();             /* 0x1f121 */
            f37de0();
            if (key88ccd) {
                screenshot();
                while (key88ccd)
                    ;
            }
            if (pal_a == 0)
                pal_a = 0x3e80;
            else
                pal_a = 0;
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
                k++;
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
            pal_b = 0xbb80;
            pal_c = pal_a;
            setpalette();
            spr_h = 0x26;
            spr_w = 0x1c;
            spr_y = y;
            sprptr = faces[face].img[frame];
            spr_x = x;
            drawsprite();
            f37de0();
            if (key88ccd) {
                screenshot();
                while (key88ccd)
                    ;
            }
            while (ticks <= 12)
                ;
            ticks2++;
            if (pal_a == 0)
                pal_a = 0x3e80;
            else
                pal_a = 0;
            if (ticks2 > 0x88)        /* 0x1f331 */
                break;
            joy88b40 = 0;
            joy88b42 = 0;
            joy88b44 = 0;
            v7d8a4 = v7d8ac;
            joy88b46 = 0;
            f377ae();
            if (v88cce || joy88b40 || joy88b42 || joy88b44 || joy88b46)
                break;
            if (key88cd0 == 1)
                return;
            if (++frame > 7)
                frame = 0;
        }
        v88b4a = 0;                   /* 0x1f3c3 */
        first += 12;
        save = pal_a;
        pal_a = 0x7d00;
        p7d714 = p6a980;
        f375cd(p6a980, first);
        pal_a = 0xbb80;
        pal_b = 0x7d00;
        pal_c = 0xbb80;
        setpalette();
        page++;
        credits_page(12, first);      /* 0x1f41c */
        pal_a = save;
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
