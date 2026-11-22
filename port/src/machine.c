/* machine.c: the hardware thread, port I/O outside the VGA, interrupt vectors, keyboard, screenshots.
 *
 * Headless runs (no window) are driven by the environment:
 *   PORT_KEYS="ms:sc,ms:sc,..."   scancodes (set 1, hex; a release is sc|0x80) queued at those times
 *   PORT_SHOTS="ms:file.png,..."  the screen as shown at those times
 *   PORT_EXIT=ms                  end the process then (exit status 0)
 * so a check can play a fixed path and compare its pictures with the original's (port/README.md). */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "machine.h"

static struct timespec t0;
static volatile int running;
static int headless, scale = 3;
static pthread_t hw;
static port_isr vec[256];
static volatile uint8_t port60, port61;

/* scancodes waiting for IRQ 1, and the BIOS buffer getch()/kbhit() read while int 9 is the BIOS's */
static uint8_t kq[256];
static volatile unsigned kq_head, kq_tail;
static uint16_t biosq[64];
static volatile unsigned bq_head, bq_tail;
static pthread_mutex_t kmu = PTHREAD_MUTEX_INITIALIZER;

void port_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fputs("port: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

uint32_t machine_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
}

static uint32_t machine_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((t.tv_sec - t0.tv_sec) * 1000000 + (t.tv_nsec - t0.tv_nsec) / 1000);
}

static uint64_t machine_us64(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)((int64_t)(t.tv_sec - t0.tv_sec) * 1000000 + (t.tv_nsec - t0.tv_nsec) / 1000);
}

/* the interrupt flag (port/include/interrupt.h) */
static pthread_mutex_t cli_mu;
static pthread_once_t cli_once = PTHREAD_ONCE_INIT;

static void cli_init(void)
{
    pthread_mutexattr_t a;

    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cli_mu, &a);
}

void port_cli(void)
{
    pthread_once(&cli_once, cli_init);
    pthread_mutex_lock(&cli_mu);
}

void port_sti(void) { pthread_mutex_unlock(&cli_mu); }

void isr_set(unsigned vec_no, port_isr fn) { vec[vec_no & 255] = fn; }
port_isr isr_get(unsigned vec_no) { return vec[vec_no & 255]; }

unsigned port_in8(unsigned port)
{
    port &= 0xffff;
    if (port >= 0x3b0 && port <= 0x3df)
        return vga_in8(port);
    switch (port) {
    case 0x60: return port60;
    case 0x61: return port61;
    case 0x201: return 0xf0;           /* game port: no joystick, buttons up, axes never time out */
    case 0x388: case 0x389: return audio_in(port);
    }
    return 0xff;                       /* nothing there (UARTs, sound cards: not emulated yet) */
}

void port_out8(unsigned port, unsigned v)
{
    port &= 0xffff;
    if (port >= 0x3b0 && port <= 0x3df)
        vga_out8(port, v);
    else if (port == 0x61)
        port61 = v;
    else if (port == 0x388 || port == 0x389)
        audio_out(port, v);
    /* 0x20 (PIC EOI), 0x40-0x43 (PIT), 0x201: nothing to do */
}

/* a 16-bit access is one bus cycle on the PC (and one I/O delay in DOSBox), the bytes to port and
 * port + 1 */
unsigned port_in16(unsigned port)
{
    port &= 0xffff;
    if (port >= 0x3b0 && port < 0x3df)
        return vga_in16(port);
    return port_in8(port) | port_in8(port + 1) << 8;
}

void port_out16(unsigned port, unsigned v)
{
    port &= 0xffff;
    if (port >= 0x3b0 && port < 0x3df)
        vga_out16(port, v);
    else {
        port_out8(port, v & 0xff);
        port_out8(port + 1, v >> 8 & 0xff);
    }
}

/* the BIOS's own int 9: scancode -> (scancode << 8 | ascii) for getch */
static const char ascii_lo[0x3a] = "\0\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";

static void bios_int9(uint8_t sc)
{
    unsigned n;

    if (sc & 0x80)
        return;
    n = (bq_tail + 1) % 64;
    if (n == bq_head)
        return;
    biosq[bq_tail] = sc << 8 | (sc < sizeof ascii_lo ? (uint8_t)ascii_lo[sc] : 0);
    bq_tail = n;
}

static volatile int text_title;       /* set by getch, shown by the hardware thread */

int getch(void)
{
    static int pending = -1;
    static int printed;
    uint16_t k;

    if (vga_text() && !printed && bq_head == bq_tail) {     /* the end screen waits for a key */
        textmode_print();
        printed = 1;
        text_title = 1;
    }
    if (pending >= 0) {
        k = pending;
        pending = -1;
        return k;
    }
    while (bq_head == bq_tail)
        usleep(1000);
    k = biosq[bq_head];
    bq_head = (bq_head + 1) % 64;
    if ((k & 0xff) == 0) {             /* extended key: 0 first, the scancode on the next call */
        pending = k >> 8;
        return 0;
    }
    return k & 0xff;
}

int kbhit(void) { return bq_head != bq_tail; }

void machine_key(uint8_t sc)
{
    pthread_mutex_lock(&kmu);
    if ((kq_tail + 1) % 256 != kq_head) {
        kq[kq_tail] = sc;
        kq_tail = (kq_tail + 1) % 256;
    }
    pthread_mutex_unlock(&kmu);
}

/* IRQ 1, on the hardware thread: the game's handler if it installed one, else the BIOS's */
static void deliver_keys(void)
{
    while (kq_head != kq_tail) {
        uint8_t sc = kq[kq_head];
        kq_head = (kq_head + 1) % 256;
        port60 = sc;
        if (vec[9])
            vec[9]();
        else
            bios_int9(sc);
    }
}

/* SDL scancode -> PC set 1 (0xe0 prefix for the grey keys: returned as 0x100 | code) */
static unsigned set1(SDL_Scancode s)
{
    static const uint8_t letters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25,
                                        0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f,
                                        0x11, 0x2d, 0x15, 0x2c};

    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z)
        return letters[s - SDL_SCANCODE_A];
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0)
        return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10)
        return 0x3b + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_RETURN: return 0x1c;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_LCTRL: return 0x1d;
    case SDL_SCANCODE_RCTRL: return 0x11d;
    case SDL_SCANCODE_LSHIFT: return 0x2a;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_RALT: return 0x138;
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_UP: return 0x148;
    case SDL_SCANCODE_DOWN: return 0x150;
    case SDL_SCANCODE_LEFT: return 0x14b;
    case SDL_SCANCODE_RIGHT: return 0x14d;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_KP_6: return 0x4d;
    case SDL_SCANCODE_KP_ENTER: return 0x11c;
    default: return 0;
    }
}

static void key_event(SDL_Scancode s, int up)
{
    unsigned c = set1(s);

    if (!c)
        return;
    if (c & 0x100)
        machine_key(0xe0);
    machine_key((uint8_t)(c | (up ? 0x80 : 0)));
}

/* ---- PNG, stored deflate blocks: no zlib needed */
static uint32_t crc_tab[256];

static uint32_t crc(uint32_t c, const uint8_t *b, size_t n)
{
    size_t i;

    if (!crc_tab[1])
        for (i = 0; i < 256; i++) {
            uint32_t r = i;
            int k;
            for (k = 0; k < 8; k++)
                r = r & 1 ? 0xedb88320u ^ r >> 1 : r >> 1;
            crc_tab[i] = r;
        }
    c = ~c;
    for (i = 0; i < n; i++)
        c = crc_tab[(c ^ b[i]) & 255] ^ c >> 8;
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *d, size_t n)
{
    uint8_t h[8];
    uint32_t c;

    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(d, 1, n, f);
    c = crc(crc(0, (const uint8_t *)type, 4), d, n);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static void write_png(const char *path, const uint32_t *rgb, int w, int h)
{
    size_t raw_n = (size_t)h * (1 + 3 * w), nblk = (raw_n + 65534) / 65535, zn = 2 + raw_n + 5 * nblk + 4, i, o = 0;
    uint8_t *raw = malloc(raw_n), *z = malloc(zn), ihdr[13];
    uint32_t a = 1, b = 0;
    FILE *f = fopen(path, "wb");
    int x, y;

    if (!f || !raw || !z)
        port_fatal("cannot write %s", path);
    for (y = 0; y < h; y++) {
        uint8_t *r = raw + (size_t)y * (1 + 3 * w);
        *r++ = 0;
        for (x = 0; x < w; x++) {
            uint32_t p = rgb[y * w + x];
            *r++ = p >> 16;
            *r++ = p >> 8;
            *r++ = p;
        }
    }
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (i = 0; i < raw_n; i += 65535) {
        size_t n = raw_n - i < 65535 ? raw_n - i : 65535;
        z[o++] = i + n == raw_n;
        z[o++] = n;
        z[o++] = n >> 8;
        z[o++] = ~n;
        z[o++] = ~n >> 8;
        memcpy(z + o, raw + i, n);
        o += n;
    }
    for (i = 0; i < raw_n; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z + o, b << 16 | a);
    o += 4;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

void machine_frame_png(const char *path)
{
    static uint32_t rgb[320 * 240];

    vga_render(rgb);
    write_png(path, rgb, vga_width(), vga_height());
}

/* ---- the scripted input and pictures of a headless run */
struct ev { uint32_t ms; unsigned sc; char path[256]; };
static struct ev keys[512], shots[64];
static int nkeys, nshots, ki, si;
static uint32_t exit_ms;

/* the events run in time order whatever order the list gives them (stable: two keys at one time keep
 * theirs) */
static void sort_events(struct ev *v, int n)
{
    for (int i = 1; i < n; i++) {
        struct ev t = v[i];
        int j = i;
        while (j > 0 && v[j - 1].ms > t.ms) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = t;
    }
}

static void parse_script(void)
{
    const char *k = getenv("PORT_KEYS"), *s = getenv("PORT_SHOTS"), *e = getenv("PORT_EXIT");
    static char buf[16384];
    char *tok, *save;

    if (k) {
        snprintf(buf, sizeof buf, "%s", k);
        for (tok = strtok_r(buf, ",", &save); tok && nkeys < 512; tok = strtok_r(NULL, ",", &save))
            if (sscanf(tok, "%u:%x", &keys[nkeys].ms, &keys[nkeys].sc) == 2)
                nkeys++;
    }
    if (s) {
        snprintf(buf, sizeof buf, "%s", s);
        for (tok = strtok_r(buf, ",", &save); tok && nshots < 64; tok = strtok_r(NULL, ",", &save))
            if (sscanf(tok, "%u:%255s", &shots[nshots].ms, shots[nshots].path) == 2)
                nshots++;
    }
    sort_events(keys, nkeys);
    sort_events(shots, nshots);
    if (e)
        exit_ms = (uint32_t)atoi(e);
}

static void *hw_main(void *arg)
{
    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_Texture *tex = NULL;
    static uint32_t rgb[320 * 240];
    uint32_t next_frame = 0;

    (void)arg;
    if (!headless) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
            port_fatal("SDL_Init: %s", SDL_GetError());
        win = SDL_CreateWindow("Wacky Wheels", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * scale, 240 * scale, SDL_WINDOW_RESIZABLE);
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
        if (!win || !ren)
            port_fatal("SDL window: %s", SDL_GetError());
        SDL_RenderSetLogicalSize(ren, 320, 240);     /* 4:3: 320x200 pixels stretched to 240 rows */
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
    }
    audio_start(headless);
    while (running) {
        uint32_t ms = machine_ms();

        if (text_title == 1 && win) {
            SDL_SetWindowTitle(win, "Wacky Wheels - the end screen is in the terminal: press a key");
            text_title = 2;
        }
        ts_tick(machine_us());
        audio_pump(machine_us64());
        while (ki < nkeys && keys[ki].ms <= ms) {
            if (keys[ki].sc & 0x100)
                machine_key(0xe0);
            machine_key((uint8_t)keys[ki].sc);
            ki++;
        }
        deliver_keys();
        while (si < nshots && shots[si].ms <= ms) {
            machine_frame_png(shots[si].path);
            fprintf(stderr, "port: %u ms: %s\n", ms, shots[si].path);
            si++;
        }
        if (exit_ms && ms >= exit_ms) {
            fprintf(stderr, "port: PORT_EXIT at %u ms\n", ms);
            audio_close();
            _exit(0);
        }
        if (!headless && ms >= next_frame) {
            SDL_Event e;
            int h = vga_height();

            next_frame = ms + 14;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) {
                    audio_close();
                    _exit(0);
                }
                if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_KEYUP)
                    key_event(e.key.keysym.scancode, e.type == SDL_KEYUP);
            }
            vga_render(rgb);
            SDL_UpdateTexture(tex, NULL, rgb, 320 * 4);
            SDL_RenderClear(ren);
            {
                SDL_Rect src = {0, 0, 320, h}, dst = {0, 0, 320, 240};
                SDL_RenderCopy(ren, tex, &src, &dst);
            }
            SDL_RenderPresent(ren);
        }
        usleep(500);
    }
    return NULL;
}

int machine_start(int headless_, int scale_)
{
    clock_gettime(CLOCK_MONOTONIC, &t0);
    headless = headless_;
    if (scale_ > 0)
        scale = scale_;
    parse_script();
    vga_set_mode(3);
    running = 1;
    return pthread_create(&hw, NULL, hw_main, NULL);
}

void machine_stop(void)
{
    running = 0;
    pthread_join(hw, NULL);
}
