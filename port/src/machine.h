/* machine.h: the emulated PC the game's code runs on (port/README.md).
 *
 * Two threads. The game thread runs the game's main(). The hardware thread owns SDL: it presents
 * the VGA, turns key events into scancodes for the game's int 9 handler, and runs the timer tasks
 * at their rates. Like real interrupts, the handlers run while the game thread is anywhere, so the
 * game's busy waits (`while (ticks <= 0x12c);`) end as they did on the PC. */
#ifndef PORT_MACHINE_H
#define PORT_MACHINE_H
#include <stdint.h>

#define DATA_BASE 0x60000u       /* WW.EXE object 2 */
#define DATA_SIZE 0x33100u
#define DATA_MAP  0x34000u       /* DATA_SIZE rounded up to pages */
#define VGA_WIN   0xa0000u       /* the 64 KB graphics window the game addresses */

int image_load(const char *exe);

/* VGA (vga.c): 4 planes of 64 KB behind the sequencer and graphics controller */
void vga_out8(unsigned port, unsigned v);
unsigned vga_in16(unsigned port);       /* one bus access for both bytes */
void vga_out16(unsigned port, unsigned v);
unsigned vga_in8(unsigned port);
uint8_t vga_read(uint32_t off);              /* off: 0..0xffff into the window */
void vga_write(uint32_t off, uint8_t v);
void vga_render(uint32_t *rgb);              /* 320x200 (or the CRTC's size) as 0x00RRGGBB */
int vga_width(void);
int vga_height(void);
void vga_set_mode(int mode);                 /* int 10h ah=0 */

/* port I/O: VGA ports to vga.c, PIT/PIC/keyboard here (machine.c) */
unsigned port_in8(unsigned port);
void port_out8(unsigned port, unsigned v);
/* audio.c: the OPL2 at 0x388/0x389 and the sound output */
void port_cli(void);                    /* the interrupt flag: port/include/interrupt.h */
void port_sti(void);
void audio_start(int headless);
void audio_out(unsigned port, unsigned v);
unsigned audio_in(unsigned port);
void audio_pump(uint64_t us);
void audio_close(void);
unsigned port_in16(unsigned port);
void port_out16(unsigned port, unsigned v);

/* the linear address space as the game's code sees it: the VGA window is not memory */
static inline int is_vga(uint32_t a) { return a - VGA_WIN < 0x10000u; }

/* interrupts: the vectors the game installs (int 21h 25h, _dos_setvect) */
typedef void (*port_isr)();
void isr_set(unsigned vec, port_isr fn);
port_isr isr_get(unsigned vec);

/* the hardware thread */
int machine_start(int headless, int scale);
void machine_stop(void);
uint32_t machine_ms(void);                   /* ms since start */
void machine_key(uint8_t scancode);          /* queue a scancode (port 60h) and raise IRQ 1 */
void machine_frame_png(const char *path);    /* write the screen as it is shown now */

/* the timer tasks (sound.c: the sound library's task manager, TS_*) */
void ts_tick(uint32_t now_us);

void port_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#endif
