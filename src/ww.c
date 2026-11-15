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
    char f22[0x82 - 0x22];
    int f82;
    char f86[0x94 - 0x86];
    short f94, f96, f98, f9a;
    short place;
    short lastcell;
    short backward;
    short lap;
    char fa4[0xc4 - 0xa4];
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
extern short v7ecb4;
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

void voice_play(int n)
{
    char *data;
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

struct player {                 /* 0x68 bytes; base 0x6AA90 is a guess (the most-used low address) */
    char f00[0x16];
    short x, y;
    char f1a[0x20 - 0x1A];
    unsigned short place;
    short lap;
    short cell;
    short face;
    char f28[0x50 - 0x28];
    int pos;
    char f54[0x68 - 0x54];
};

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
extern short v88bb2;
struct face { char *img[38]; };
extern struct face faces[];
void print_at(int x, int y, char *s, int mode);

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
void f375cd(void);
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

/* ---- the menu pointers (0x73938) */

struct ent {
    short id;
    short f2;
    short x, y;
    short f8;
    short frame;
};

extern struct ent ents[8];
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
